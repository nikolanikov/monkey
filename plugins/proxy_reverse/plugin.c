#include <fcntl.h>
#include <unistd.h>

#include "MKPlugin.h"

MONKEY_PLUGIN("proxy_reverse", "Reverse Proxy", "0.1", MK_PLUGIN_STAGE_20);

static int log;

int _mkp_init(struct plugin_api **api, char *confdir)
{
	struct plugin_api *mk_api = *api;

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
						mk_err("CGI: Invalid configuration key");
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

void _mkp_exit(void)
{
	close(log);
}

// TODO remove _mkp_stage_20 and rename func to _mkp_stage_20 when debugging is no longer necessary
int func(struct client_session *cs, struct session_request *sr)
{
	write(log, sr->body.data, sr->body.len);
	write(log, "\r\n", 2);
}
int _mkp_stage_20(struct client_session *cs, struct session_request *sr)
{
	return func(cs, sr);
}
