#include <fcntl.h>
#include <unistd.h>

#include "types.h"

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

/*static struct plugin_api *api_;*/

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

struct plugin_api *mk_api;

int _mkp_init(struct plugin_api **api, char *confdir)
{
	mk_api = *api;

	pthread_key_create(&proxy_key, 0);

	char *file = NULL;
	unsigned long len;
	struct mk_config *conf;
	struct mk_config_section *section;
	char *save = 0;

	mk_api->str_build(&file, &len, "%sproxy_reverse.conf", confdir);
	conf = mk_api->config_create(file);
	section = mk_api->config_section_get(conf, "ProxyReverse");

	if (section)
	{
		struct mk_list *head;
		struct mk_list *line;
		struct mk_list *head_match;
		struct mk_config_entry *entry;
		struct mk_string_line *entry_match;

		mk_list_foreach(head, &section->entries)
		{
			entry = mk_list_entry(head, struct mk_config_entry, _head);
			if (strncasecmp(entry->key, "SavePath", strlen(entry->key)) == 0)
			{
				line = mk_api->str_split_line(entry->val);
				if (!line) continue;

				mk_list_foreach(head_match, line)
				{
					entry_match = mk_list_entry(head_match, struct mk_string_line, _head);
					if (!entry_match)
					{
						mk_err("ProxyReverse: Invalid configuration key");
						exit(EXIT_FAILURE);
					}

					save = strdup(entry_match->val);

					break;
				}
			}
		}
	}

	free(file);
	mk_api->config_free(conf);

	if (save)
	{
		log = creat(save, 0644);
		free(save);
	}
	else ; // TODO
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

int proxy_peer_add(struct dict *dict, int fd, struct proxy_peer *peer)
{
	struct string key = string((char *)&fd, sizeof(int));
	return dict_add(dict, &key, peer);
}

struct proxy_peer *proxy_peer_get(struct dict *dict, int fd)
{
	struct string key = string((char *)&fd, sizeof(fd));
	return dict_get(dict, &key);
}

struct proxy_peer *proxy_peer_remove(struct dict *dict, int fd)
{
	struct string key = string((char *)&fd, sizeof(fd));
	return dict_remove(dict, &key);
}

// TODO remove _mkp_stage_30 and rename func to _mkp_stage_30 when debugging is no longer necessary
int func(struct plugin *plugin, struct client_session *cs, struct session_request *sr)
{
	struct proxy_context *context = pthread_getspecific(proxy_key);

	fprintf(stderr, "STAGE 30\n");

	int slave = mk_api->socket_connect("127.0.0.1", 8080);
	if (slave < 0) ; // TODO
	mk_api->socket_set_nonblocking(slave);

	struct proxy_peer *peer = malloc(sizeof(struct proxy_peer));
	if (!peer) return ERROR_MEMORY;

	peer->fd_client = cs->socket;
	peer->fd_slave = slave;
	peer->mode_client = 0;
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

	//return MK_PLUGIN_RET_END;
	return MK_PLUGIN_RET_CONTINUE;
}
int _mkp_stage_30(struct plugin *plugin, struct client_session *cs, struct session_request *sr)
{
	return func(plugin, cs, sr);
}

int _mkp_event_read(int socket)
{
	struct proxy_context *context = pthread_getspecific(proxy_key);
	struct proxy_peer *peer;

	peer = proxy_peer_get(&context->slave, socket);
	if (!peer) return MK_PLUGIN_RET_EVENT_NEXT;

	fprintf(stderr, "READ slave\n");

	/*if (!peer->response.total)
	{
		peer->mode_client |= MK_EPOLL_WRITE;
		mk_api->event_socket_change_mode(peer->fd_client, peer->mode_client, MK_EPOLL_LEVEL_TRIGGERED);
	}*/

	size_t left = peer->response.size - peer->response.total;
	if (!left)
		if (!request_buffer_adjust(peer->response.size + 1))
		{
			// Don't poll for reading until we have free space in the buffer.
			//peer->mode_slave &= ~MK_EPOLL_READ;
			//mk_api->event_socket_change_mode(peer->fd_slave, peer->mode_slave, MK_EPOLL_LEVEL_TRIGGERED);
			return MK_PLUGIN_RET_EVENT_OWNED;
		}
	left = peer->response.size - peer->response.total;

	ssize_t size = read(peer->fd_slave, peer->response.buffer + peer->response.total, left);
	if (size <= 0) return MK_PLUGIN_RET_EVENT_CLOSE;
	peer->response.total += size;

	mk_api->event_socket_change_mode(peer->fd_client, MK_EPOLL_WRITE, MK_EPOLL_LEVEL_TRIGGERED);

	return MK_PLUGIN_RET_EVENT_OWNED;
}

int _mkp_event_write(int socket)
{
	struct proxy_context *context = pthread_getspecific(proxy_key);
	struct proxy_peer *peer;
	ssize_t size;

	peer = proxy_peer_get(&context->client, socket);
	if (peer)
	{
		fprintf(stderr, "WRITE client\n");

		/* Write response to the client. Don't poll for writing if we don't have anything to write. */
		if (peer->response.index < peer->response.total)
		{
			size = write(peer->fd_client, peer->response.buffer + peer->response.index, peer->response.total - peer->response.index);
			if (size < 0) return MK_PLUGIN_RET_EVENT_CLOSE;
			fprintf(stderr, "WRITE client %u written\n", (unsigned)size);
			peer->response.index += size;
		}
		else
		{
			fprintf(stderr, "WRITE client canceled\n");
			mk_api->event_socket_change_mode(peer->fd_client, 0, MK_EPOLL_LEVEL_TRIGGERED);
		}

		/*peer->mode_client &= ~MK_EPOLL_WRITE;
		mk_api->event_socket_change_mode(peer->fd_client, peer->mode_client, MK_EPOLL_LEVEL_TRIGGERED);*/
	}
	else
	{
		fprintf(stderr, "WRITE slave\n");

		peer = proxy_peer_get(&context->slave, socket);
		if (!peer) return MK_PLUGIN_RET_EVENT_NEXT;

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
			if (size < 0) return MK_PLUGIN_RET_EVENT_CLOSE;
			peer->request_index += size;
		}
		else mk_api->event_socket_change_mode(peer->fd_slave, MK_EPOLL_READ, MK_EPOLL_LEVEL_TRIGGERED);

		// Make sure we poll for incoming data from the slave server.
		/*peer->mode_slave |= MK_EPOLL_READ;
		mk_api->event_socket_change_mode(peer->fd_slave, peer->mode_slave, MK_EPOLL_LEVEL_TRIGGERED);

		// Don't poll for writing since we don't have anything to write.
		peer->mode_slave &= ~MK_EPOLL_WRITE;
		mk_api->event_socket_change_mode(peer->fd_slave, peer->mode_slave, MK_EPOLL_LEVEL_TRIGGERED);*/
	}

	return MK_PLUGIN_RET_EVENT_OWNED;
}

void proxy_end(int fd)
{
	struct proxy_context *context = pthread_getspecific(proxy_key);

	// TODO
	//  close client socket with RST
	//  for slave socket: mk_api->event_del(fd);
	//  close slave socket with close
	//  do something to the other socket
	//  maybe closes should be performed externally and we need to return appropriate value here

	// TODO finish this
	struct proxy_peer *peer = proxy_peer_remove(&context->slave, fd);
	if (!peer)
	{
		peer = proxy_peer_remove(&context->client, fd);
		if (!peer) return; // nothing to do
	}

	mk_api->event_del(peer->fd_client);
	mk_api->event_del(peer->fd_slave);

	mk_api->socket_close(peer->fd_client);
	mk_api->socket_close(peer->fd_slave);

	free(peer->response.buffer);
	free(peer);
}

int _mkp_event_close(int fd)
{
	fprintf(stderr, "CLOSE\n");
	proxy_end(fd);
	return MK_PLUGIN_RET_END;
}

int _mkp_event_error(int fd)
{
	fprintf(stderr, "ERROR\n");
	proxy_end(fd);
	return MK_PLUGIN_RET_END;
}
