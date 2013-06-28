//IMPORTANT, not tested or compiled, just prototype
#define _GNU_SOURCE

#include "MKPlugin.h"
#include "config.h"

static void free_proxy_server_entry_array(struct proxy_server_entry_array *server_list)
{
	int i=0;
	if(!server_list)return ;
	for(;i<server_list->length;i++)
		{
			mk_api->mem_free(server_list->proxy_server_entry[i]->hostname);
		}
	mk_api->mem_free(server_list);
}

static struct proxy_server_entry_array *proxy_parse_ServerList(char *server_addr)
{
	char *tmp;
	int server_num=0;
	struct mk_list *line,*head;
	struct mk_list *server_list=mk_api->str_split_line(server_addr);
	struct proxy_server_entry_array *proxy_server_array=0;
	
	if(!server_addr)return 0;
	
	line = mk_api->str_split_line(server_addr);
				if (!line)return 0;
				
				mk_list_foreach(head, line)
				{
				server_num++;
				}

				if(!server_num)return 0;
				
				proxy_server_array=mk_api->mem_alloc( sizeof(struct proxy_server_entry_array) + sizeof(struct proxy_server_entry)*server_num );
				if(!proxy_server_array)return 0;
				proxy_server_array->length=server_num;
				
				server_num=0;
				mk_list_foreach(head, line)
				{
					entry_match = mk_list_entry(head, struct mk_string_line, _head);
					if (!entry_match)
					{
						mk_err("ProxyReverse: Invalid configuration ServerList");
						mk_api->mem_free(proxy_server_array);
						return 0;
					}
					
					tmp = memchr(entry_match->val, ':', entry_match->len);
					if(!tmp)
					{
						mk_err("ProxyReverse: Invalid configuration ServerList");
						mk_api->mem_free(proxy_server_array);
						return 0;
					}
					
					*tmp = '\0';
					
					proxy_server_array->entry[server_num].hostname=strdup(entry_match->val);
					proxy_server_array->entry[server_num].port=strtol(tmp+1,0,10);

					server_num++;
				}
	
	mk_api->str_split_free(server_list);
	
	return proxy_server_array;
}

static struct proxy_server_entry_array *proxy_server_entry_array_dup(struct proxy_server_entry_array *array_to_dup)
{
struct proxy_server_entry_array *proxy_server_array;
int i=0;
	proxy_server_array=mk_api->mem_alloc( sizeof(struct proxy_server_entry_array) + sizeof(struct proxy_server_entry)*array_to_dup->length );
	if(!proxy_server_array)return 0;
	proxy_server_array->length=array_to_dup->length;
	
	for(;i<proxy_server_array->length;i++)
	{
	proxy_server_array->entry[i].hostname=strdup(array_to_dup->entry[i].hostname);
	proxy_server_array->entry[i].port=array_to_dup->entry[i].port;
	}
	
	return proxy_server_array;
}

static void proxy_config_read_defaults(struct proxy_cnf_default_values *default_values, struct mk_config_section *section)
{
	char *server_addr;
	char *load_balancer;
	default_values->balancer_type=0;
	default_values->server_list=0;
	
	load_balancer=mk_api->config_section_getval(section, "LoadBalancer", MK_CONFIG_VAL_STR);
	if(load_balancer)
		{
			//CHECK currently I'm looking just for first char, because every type load balancer is starting with different char
			//PROS: it makes the code smaller and a little faster, for emb devices the static file size is important because of the small flash size;
			//CONS: syntax checks for the LoadBalancer names are not possible.
			if(load_balancer[0]=='F')default_values->balancer_type=FirstAlive;
			else if(load_balancer[0]=='R')default_values->balancer_type=RoundRobin;
			else if(load_balancer[0]=='W')default_values->balancer_type=WRoundRobin;
			else if(load_balancer[0]=='H')default_values->balancer_type=Hash;
		
		mk_api->mem_free(load_balancer);
		}
	
	server_addr=mk_api->config_section_getval(section, "ServerList", MK_CONFIG_VAL_STR);
	if(server_addr)default_values->server_list=proxy_parse_ServerAddr(server_addr);
}



static struct proxy_entry_array *proxy_config_read_entries(struct proxy_cnf_default_values *default_values, struct mk_config *config, int entry_num)
{
	struct mk_config *config;
    struct mk_config_section *section;
	struct mk_list *head;
	struct proxy_entry_array *entry_array=0;
	struct proxy_cnf_default_values tmp_values;
	
	entry_array=mk_api->mem_alloc( sizeof(struct proxy_entry_array) + sizeof(struct proxy_entry)*entry_num );
	if(!entry_array)return 0;
	entry_array->length=entry_num;
	entry_num=0;
	mk_list_foreach(head, &config->sections) {
		section = mk_list_entry(head, struct mk_config_section, _head);

		if (!strcasecmp(section->name, "PROXY_ENTRY")) {
			
			//read values that are the same with the default ones
			proxy_config_read_defaults(&tmp_values,section);
				//Config Checks
				if(!tmp_values.balancer_type && !default_values->balancer_type ) {
					mk_err("ProxyReverse: PROXY_ENTRY doesn't have LoadBalancer specified.");
					goto error;
					}
				else if(!tmp_values.server_list && !default_values->server_list ) {
					mk_err("ProxyReverse: PROXY_ENTRY doesn't have ServerList specified.");
					goto error;
				}
				
			if(tmp_values.balancer_type)entry_array.entry[entry_num].balancer_type=tmp_values.balancer_type;
			else entry_array.entry[entry_num].balancer_type=default_values->balancer_type;
			
			if(tmp_values.server_list)entry_array.entry[entry_num].server_list=proxy_server_entry_array_dup(tmp_values.server_list);
			else entry_array.entry[entry_num].server_list=proxy_server_entry_array_dup(default_values->server_list);
			//read matches
			
			free_proxy_server_entry_array(tmp_values.server_list);
			
			//TODO read the Matches..
			
			entry_num++;
		}
		
	}
	
	return entry_array;
	error:
	
	mk_api->mem_free(entry_array);
	return 0;
}

struct proxy_entry_array *cgi_read_config(const char * const path)
{
	struct proxy_cnf_default_values default_values;
	//TODO to check the mallocs for errors
	char *conf_path = NULL;
    struct mk_config *config;
    struct mk_config_section *section;
	struct mk_list *head;
	struct proxy_entry_array *entry_array=0;
	int proxy_entries=0;
	
	mk_api->str_build(&conf_path, &len, "%s/proxy_reverse.conf", confdir);
	config = mk_api->config_create(conf_path);
	mk_api->mem_free(conf_path);
	
	mk_list_foreach(head, &config->sections) {
		section = mk_list_entry(head, struct mk_config_section, _head);

		if (!strcasecmp(section->name, "PROXY_ENTRY")) {
			proxy_entries++;
		}
		else if(!strcasecmp(section->name, "PROXY_DEFAULTS"))
		{
			proxy_config_read_defaults(&default_values,section);
		}
	}
	
	if(!proxy_entries)
	{
		free_proxy_server_entry_array(default_values.server_list);
		mk_err("ProxyReverse: There aren't any PROXY_ENTRY in the configuration file.");
		return ;//ERROR
	}
	
	proxy_config_read_defaults(&default_values,section);
	
	entry_array=proxy_config_read_entries(&default_values,config,proxy_entries);
	
	free_proxy_server_entry_array(default_values.server_list);
	
	return entry_array;
}