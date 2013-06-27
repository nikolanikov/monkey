#include <fcntl.h>
#include <unistd.h>

#include "types.h"

#include "MKPlugin.h"

MONKEY_PLUGIN("proxy_reverse", "Reverse Proxy", "0.1", MK_PLUGIN_STAGE_30 | MK_PLUGIN_CORE_THCTX);

struct proxy_context
{
	struct dict client, slave;
};
static pthread_key_t proxy_key;

//static struct plugin_api *api_;

struct proxy_pair
{
	int fd_client, fd_slave;
	struct session_request *sr;
	// TODO slave response
};

static int log;

int _mkp_init(struct plugin_api **api, char *confdir)
{
	struct plugin_api *mk_api = *api;

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
	struct proxy_context *context = malloc(sizeof(context));
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

bool proxy_peer_add(struct dict *dict, int fd, void *value)
{
	struct string key = string((char *)&fd, sizeof(fd));
	return dict_add(dict, &key, value);
}

void *proxy_peer_get(struct dict *dict, int fd)
{
	struct string key = string((char *)&fd, sizeof(fd));
	return dict_get(dict, &key);
}

// TODO remove _mkp_stage_30 and rename func to _mkp_stage_30 when debugging is no longer necessary
int func(struct plugin *plugin, struct client_session *cs, struct session_request *sr)
{
	/*
	struct proxy_context *context = pthread_getspecific(proxy_key);

	int slave = mk_api->socket_connect("127.0.0.1", 8080);
	if (slave < 0) ; // TODO
	mk_api->socket_set_nonblocking(slave);
	mk_api->event_add(slave, MK_EPOLL_READ | MK_EPOLL_WRITE, 0, MK_EPOLL_LEVEL_TRIGGERED); // TODO check third argument

	//mk_api->socket_close(*slave);

	if (proxy_fd_add(&context->client, cs->socket, 1)) ; // TODO
	if (proxy_fd_add(&context->slave, slave, 1)) ; // TODO
	*/

	write(log, sr->body.data, sr->body.len);
	write(log, "\r\n", 2);

	return MK_PLUGIN_RET_NOT_ME;
	//return MK_PLUGIN_RET_END;
}
int _mkp_stage_30(struct plugin *plugin, struct client_session *cs, struct session_request *sr)
{
	return func(plugin, cs, sr);
}

int _mkp_event_read(int socket)
{
	/*struct proxy_context *context = pthread_getspecific(proxy_key);
	void *value;

	value = proxy_peer_get(&context->client, socket);
	if (!value)
	{
		value = proxy_peer_get(&context->slave, socket);
		if (!value) return MK_PLUGIN_RET_EVENT_NEXT;
	}*/

	return MK_PLUGIN_RET_EVENT_NEXT;
	//return MK_PLUGIN_RET_EVENT_OWNED;
}

int _mkp_event_write(int socket)
{
	return MK_PLUGIN_RET_EVENT_NEXT;
}

void proxy_end(int fd)
{
	// TODO
	//  close client socket with RST
	//  for slave socket: mk_api->event_del(fd);
	//  close slave socket with close
	//  do something to the other socket
	//  maybe closes should be performed externally and we need to return appropriate value here
}

int _mkp_event_close(int fd)
{
	proxy_end(fd);
	return MK_PLUGIN_RET_EVENT_NEXT;
}

int _mkp_event_error(int fd)
{
	proxy_end(fd);
	return MK_PLUGIN_RET_EVENT_NEXT;
}
