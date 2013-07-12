#include <fcntl.h>
#include <unistd.h>

#include "types.h"
#include "regex.h"
#include "config.h"
#include "MKPlugin.h"

#define RESPONSE_BUFFER_MIN 4096
#define RESPONSE_BUFFER_MAX 65536

MONKEY_PLUGIN("proxy_reverse", "Reverse Proxy", "0.1", MK_PLUGIN_STAGE_30 | MK_PLUGIN_CORE_THCTX);

#include <stdio.h>

/* TODO fix comments to work with ANSI C */

struct proxy_context
{
	struct dict client, slave;
};
static pthread_key_t proxy_key;

struct plugin_api *mk_api;

struct proxy_peer
{
	int fd_client, fd_slave;
	int mode_client, mode_slave;
	struct session_request *sr;
	size_t request_index;
	struct
	{
		char *buffer;
		size_t size, index, total;
	} response;
};

static int log;

static struct proxy_server_entry *slave;

static bool response_buffer_adjust(struct proxy_peer *peer, size_t size)
{
	/* Check buffer size and adjust it if necessary.
	   RESPONSE_BUFFER_MIN <= size <= RESPONSE_BUFFER_MAX
	   size % RESPONSE_BUFFER_MIN == 0 */
	if (size > RESPONSE_BUFFER_MAX) return 0;
	if (size < RESPONSE_BUFFER_MIN) size = RESPONSE_BUFFER_MIN;
	else size = (size + RESPONSE_BUFFER_MIN - 1) & ~(RESPONSE_BUFFER_MIN - 1);

	if (!peer->response.size)
	{
		peer->response.buffer = malloc(size);
		if (!peer) return false;
	}
	else if (peer->response.size < size)
	{
		/* Make sure the data is at the beginning of the buffer. */
		if (peer->response.index)
		{
			size_t available = peer->response.total - peer->response.index;
			if (available) memmove(peer->response.buffer, peer->response.buffer + peer->response.index, available);
			peer->response.total = available;
			peer->response.index = 0;
		}

		peer->response.buffer = realloc(peer->response.buffer, size);
		if (!peer) return false;
	}
	else return true;

	peer->response.size = size;

	return true;
}

static int proxy_peer_add(struct dict *dict, int fd, struct proxy_peer *peer)
{
	struct string key = string((char *)&fd, sizeof(int));
	return dict_add(dict, &key, peer);
}

static struct proxy_peer *proxy_peer_get(struct dict *dict, int fd)
{
	struct string key = string((char *)&fd, sizeof(fd));
	return dict_get(dict, &key);
}

static struct proxy_peer *proxy_peer_remove(struct dict *dict, int fd)
{
	struct string key = string((char *)&fd, sizeof(fd));
	return dict_remove(dict, &key);
}

static int proxy_close(int fd)
{
	struct proxy_context *context = pthread_getspecific(proxy_key);

	// TODO
	//  close client socket with RST
	//  for slave socket: mk_api->event_del(fd);
	//  close slave socket with close
	//  do something to the other socket
	//  maybe closes should be performed externally and we need to return appropriate value here

	struct proxy_peer *peer = proxy_peer_remove(&context->slave, fd);
	if (peer) proxy_peer_remove(&context->client, peer->fd_client);
	else
	{
		peer = proxy_peer_remove(&context->client, fd);
		if (!peer) return MK_PLUGIN_RET_EVENT_NEXT; // nothing to do
		proxy_peer_remove(&context->slave, peer->fd_slave);
	}

	mk_api->event_del(peer->fd_client);
	mk_api->event_del(peer->fd_slave);

	if (fd == peer->fd_client) mk_api->socket_close(peer->fd_slave);
	else mk_api->socket_close(peer->fd_client);

	free(peer->response.buffer);
	free(peer);

	return MK_PLUGIN_RET_EVENT_CLOSE;
}

static int slave_connect(void)
{
	/* TODO choose slave based on the config file and on the algorithm used */
	int socket = mk_api->socket_connect(slave->hostname, slave->port);
	if (socket < 0) ; // TODO
	mk_api->socket_set_nonblocking(socket);
	return socket;
}

int _mkp_init(struct plugin_api **api, char *confdir)
{
	mk_api = *api;

	pthread_key_create(&proxy_key, 0);

	struct proxy_entry_array *config = proxy_reverse_read_config(confdir);
	if (!config->length) return -1;

	struct proxy_server_entry_array *entry = config->entry[0].server_list;
	if (!entry->length) return -1;

	slave = entry->entry;

	return 0;
}

/*int _mkp_core_prctx(struct server_config *config)
{
	return 0;
}*/

void _mkp_core_thctx(void)
{
	struct proxy_context *context = malloc(sizeof(struct proxy_context));
	if (!context)
	{
		mk_err("ProxyReverse: Failed to allocate proxy reverse context.");
		abort();
	}

	if (!dict_init(&context->client, 4)) ; // TODO
	if (!dict_init(&context->slave, 4)) ; // TODO

	pthread_setspecific(proxy_key, context);
}

void _mkp_exit(void)
{
	close(log);
}

int _mkp_stage_10(unsigned int socket, struct sched_connection *conx)
{fprintf(stderr, "10\n"); return MK_PLUGIN_RET_NOT_ME;}
int _mkp_stage_20(struct client_session *cs, struct session_request *sr)
{fprintf(stderr, "20\n"); return MK_PLUGIN_RET_NOT_ME;}
int _mkp_stage_40(struct client_session *cs, struct session_request *sr)
{fprintf(stderr, "40\n"); return MK_PLUGIN_RET_CONTINUE;}
int _mkp_stage_50(int sockfd)
{fprintf(stderr, "50\n"); return MK_PLUGIN_RET_NOT_ME;}

int _mkp_stage_30(struct plugin *plugin, struct client_session *cs, struct session_request *sr)
{
	struct proxy_context *context = pthread_getspecific(proxy_key);

	fprintf(stderr, "30\n");

	struct proxy_peer *peer = proxy_peer_get(&context->client, cs->socket);
	if (peer)
	{
		fprintf(stderr, "RESTAGE\n");

		peer->request_index = 0;

		peer->mode_client = MK_EPOLL_SLEEP;
		peer->mode_slave = MK_EPOLL_WRITE;
		peer->sr = sr;

		mk_api->event_socket_change_mode(peer->fd_client, peer->mode_client, MK_EPOLL_LEVEL_TRIGGERED);
		mk_api->event_socket_change_mode(peer->fd_slave, peer->mode_slave, MK_EPOLL_LEVEL_TRIGGERED);

		peer->response.index = 0;
		peer->response.total = 0;
	}
	else
	{
		int slave = slave_connect();
		if (slave < 0) ; // TODO

		peer = malloc(sizeof(struct proxy_peer));
		if (!peer) return MK_PLUGIN_RET_CLOSE_CONX;

		peer->fd_client = cs->socket;
		peer->fd_slave = slave;
		peer->mode_client = MK_EPOLL_SLEEP;
		peer->mode_slave = MK_EPOLL_WRITE;
		peer->sr = sr;

		peer->request_index = 0;

		mk_api->event_socket_change_mode(peer->fd_client, peer->mode_client, MK_EPOLL_LEVEL_TRIGGERED);
		mk_api->event_add(peer->fd_slave, peer->mode_slave, 0, MK_EPOLL_LEVEL_TRIGGERED); // TODO check third argument

		peer->response.buffer = 0;
		peer->response.size = 0;
		peer->response.index = 0;
		peer->response.total = 0;
		response_buffer_adjust(peer, RESPONSE_BUFFER_MIN);

		if (proxy_peer_add(&context->client, cs->socket, peer)) ; // TODO
		if (proxy_peer_add(&context->slave, slave, peer)) ; // TODO
	}

	return MK_PLUGIN_RET_CONTINUE;
}

int _mkp_event_read(int socket)
{
	struct proxy_context *context = pthread_getspecific(proxy_key);
	struct proxy_peer *peer;

	peer = proxy_peer_get(&context->slave, socket);
	if (peer)
	{
		// We can read from the slave server.

		fprintf(stderr, "  <- S\n");

		/*if (!peer->response.total)
		{
			peer->mode_client |= MK_EPOLL_WRITE;
			mk_api->event_socket_change_mode(peer->fd_client, peer->mode_client, MK_EPOLL_LEVEL_TRIGGERED);
		}*/

		size_t left = peer->response.size - peer->response.total;
		if (!left)
		{
			if (!response_buffer_adjust(peer, peer->response.size + 1))
			{
				// Don't poll for reading until we have free space in the buffer.
				//peer->mode_slave &= ~MK_EPOLL_READ;
				//mk_api->event_socket_change_mode(peer->fd_slave, peer->mode_slave, MK_EPOLL_LEVEL_TRIGGERED);
				//return MK_PLUGIN_RET_EVENT_OWNED;
				return MK_PLUGIN_RET_EVENT_NEXT;
			}
			left = peer->response.size - peer->response.total;
		}

		ssize_t size = read(peer->fd_slave, peer->response.buffer + peer->response.total, left);
		if (size <= 0) return proxy_close(peer->fd_slave);
		peer->response.total += size;

		mk_api->event_socket_change_mode(peer->fd_client, MK_EPOLL_WRITE, MK_EPOLL_LEVEL_TRIGGERED);

		return MK_PLUGIN_RET_EVENT_OWNED;
	}
	else
	{
		// We can read from a client.

		peer = proxy_peer_get(&context->client, socket);
		if (peer)
		{
			// TODO this doesn't work
			fprintf(stderr, "C ->  \n");
			mk_api->event_socket_change_mode(peer->fd_client, MK_EPOLL_RW, MK_EPOLL_LEVEL_TRIGGERED);
			return MK_PLUGIN_RET_EVENT_NEXT;
		}
		else return MK_PLUGIN_RET_EVENT_NEXT;
	}
}

int _mkp_event_write(int socket)
{
	struct proxy_context *context = pthread_getspecific(proxy_key);
	struct proxy_peer *peer;
	ssize_t size;

	peer = proxy_peer_get(&context->client, socket);
	if (peer)
	{
		// We can write to the client.

		fprintf(stderr, "C <-  \n");

		/* Write response to the client. Don't poll for writing if we don't have anything to write. */
		if (peer->response.index < peer->response.total)
		{
			size = write(peer->fd_client, peer->response.buffer + peer->response.index, peer->response.total - peer->response.index);
			if (size < 0) return proxy_close(peer->fd_client);
			peer->response.index += size;

			if (peer->response.index == peer->response.total)
			{
				mk_api->http_request_end(socket);
				return MK_PLUGIN_RET_EVENT_CONTINUE;
			}

			return MK_PLUGIN_RET_EVENT_OWNED;
		}

		fprintf(stderr, "C <X  \n");
		mk_api->event_socket_change_mode(peer->fd_client, MK_EPOLL_READ, MK_EPOLL_LEVEL_TRIGGERED);

		return MK_PLUGIN_RET_EVENT_CONTINUE;

		/*peer->mode_client &= ~MK_EPOLL_WRITE;
		mk_api->event_socket_change_mode(peer->fd_client, peer->mode_client, MK_EPOLL_LEVEL_TRIGGERED);*/
	}
	else
	{
		peer = proxy_peer_get(&context->slave, socket);
		if (!peer) return MK_PLUGIN_RET_EVENT_NEXT;

		// We can write to the slave server.

		fprintf(stderr, "  -> S\n");

		// Make sure we poll for incoming data from the slave server.
		/*if (!(peer->mode_slave & MK_EPOLL_READ))
		{
			peer->mode_slave |= MK_EPOLL_READ;
			mk_api->event_socket_change_mode(peer->fd_slave, peer->mode_slave, MK_EPOLL_LEVEL_TRIGGERED);
		}*/

		/* Write request to the slave server. */
		size_t total = peer->sr->body.len + sizeof("\r\n\r\n") - 1 + peer->sr->data.len;
		if (peer->request_index < total)
		{
			size = write(peer->fd_slave, peer->sr->body.data + peer->request_index, total - peer->request_index);
			if (size < 0) return proxy_close(peer->fd_slave);
			peer->request_index += size;
		}

		if (peer->request_index == total)
		{
			//mk_api->event_socket_change_mode(peer->fd_client, MK_EPOLL_WRITE, MK_EPOLL_LEVEL_TRIGGERED);
			mk_api->event_socket_change_mode(peer->fd_slave, MK_EPOLL_READ, MK_EPOLL_LEVEL_TRIGGERED);
		}

		/* // Make sure we poll for incoming data from the slave server.
		peer->mode_slave |= MK_EPOLL_READ;
		mk_api->event_socket_change_mode(peer->fd_slave, peer->mode_slave, MK_EPOLL_LEVEL_TRIGGERED);

		// Don't poll for writing since we don't have anything to write.
		peer->mode_slave &= ~MK_EPOLL_WRITE;
		mk_api->event_socket_change_mode(peer->fd_slave, peer->mode_slave, MK_EPOLL_LEVEL_TRIGGERED);*/
	}

	return MK_PLUGIN_RET_EVENT_OWNED;
}

int _mkp_event_close(int fd)
{
	fprintf(stderr, "CLOSE\n");
	return proxy_close(fd);
}

int _mkp_event_error(int fd)
{
	fprintf(stderr, "ERROR\n");
	return proxy_close(fd);
}
