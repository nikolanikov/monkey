/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*  Monkey HTTP Daemon
 *  ------------------
 *  Copyright (C) 2001-2011, Eduardo Silva P. <edsiper@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <dirent.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>

#include "monkey.h"
#include "mk_config.h"
#include "mk_string.h"
#include "mk_utils.h"
#include "mk_mimetype.h"
#include "mk_info.h"
#include "mk_memory.h"
#include "mk_server.h"
#include "mk_plugin.h"
#include "mk_macros.h"

/* Print a specific error */
static void mk_config_print_error_msg(char *variable, char *path)
{
    mk_err("Error in %s variable under %s, has an invalid value",
           variable, path);
    exit(EXIT_FAILURE);
}

/* Raise a configuration schema error */
void mk_config_error(const char *path, int line, const char *msg)
{
    mk_err("File %s", path);
    mk_err("Error in line %i: %s", line, msg);
    exit(EXIT_FAILURE);
}

/* Returns a configuration section by [section name] */
struct mk_config_section *mk_config_section_get(struct mk_config *conf, 
                                                const char *section_name)
{
    struct mk_config_section *section;

    section = conf->section;
    while (section) {
        if (strcasecmp(section->name, section_name) == 0) {
            return section;
        }
        section = section->next;
    }

    return NULL;
}

/* Register a new section into the configuration struct */
void mk_config_section_add(struct mk_config *conf, char *section_name)
{
    struct mk_config_section *new, *aux;

    /* Alloc section node */
    new = mk_mem_malloc(sizeof(struct mk_config_section));
    new->name = mk_string_dup(section_name);
    new->entry = NULL;
    new->next = NULL;
    
    if (!conf->section) { 
        conf->section = new;
        return;
    }

    /* go to last section available */
    aux = conf->section;
    while (aux->next) {
        aux = aux->next;
    }

    aux->next = new;
    return;
}

/* Register a key/value entry in the last section available of the struct */
void mk_config_entry_add(struct mk_config *conf, 
                         const char *key, const char *val)
{
    struct mk_config_section *section;
    struct mk_config_entry *aux_entry, *new_entry;

    if (!conf->section) {
        mk_err("Error: there are not sections available!");
    }

    /* Go to last section */
    section = conf->section;
    while (section->next) {
        section = section->next;
    }

    /* Alloc new entry */
    new_entry = mk_mem_malloc(sizeof(struct mk_config_entry));
    new_entry->key = mk_string_dup(key);
    new_entry->val = mk_string_dup(val);
    new_entry->next = NULL;

    /* Add first entry */
    if (!section->entry) {
        section->entry = new_entry;
        return;
    }

    /* Go to last entry */
    aux_entry = section->entry;
    while (aux_entry->next) {
        aux_entry = aux_entry->next;
    }

    aux_entry->next = new_entry;
}

struct mk_config *mk_config_create(const char *path)
{
    int i;
    int len;
    int line = 0;
    int indent_len = -1;
    char buf[255];
    char *section = 0;
    char *indent = 0;
    char *key, *val;
    struct mk_config *conf = 0;
    FILE *f;

    /* Open configuration file */
    if ((f = fopen(path, "r")) == NULL) {
        mk_warn("Config: I cannot open %s file", path);
        return NULL;
    }

    /* Alloc configuration node */
    conf = mk_mem_malloc(sizeof(struct mk_config));
    conf->created = time(NULL);
    conf->file = mk_string_dup(path);
    conf->section = NULL;

    /* looking for configuration directives */
    while (fgets(buf, 255, f)) {
        len = strlen(buf);
        if (buf[len - 1] == '\n') {
            buf[--len] = 0;
            if (len && buf[len - 1] == '\r') {
                buf[--len] = 0;
            }
        }
        
        /* Line number */
        line++;

        if (!buf[0]) {
            continue;
        }

        /* Skip commented lines */
        if (buf[0] == '#') {
            if (section) {
                mk_mem_free(section);
                section = NULL;
            }
            continue;
        }
        
        /* Section definition */
        if (buf[0] == '[') {
            int end = -1;
            end = mk_string_char_search(buf, ']', len);
            if (end > 0) {
                section = mk_string_copy_substr(buf, 1, end);
                mk_config_section_add(conf, section);
                continue;
            }
            else {
                mk_config_error(path, line, "Bad header definition");
            }
        }
        else {
            /* No separator defined */
            if (!indent) {
                i = 0;

                do { i++; } while (i < len && isblank(buf[i]));

                indent = mk_string_copy_substr(buf, 0, i);
                indent_len = strlen(indent);

                /* Blank indented line */
                if (i == len) {
                    continue;
                }
            }

            /* Validate indentation level */
            if (strncmp(buf, indent, indent_len) != 0 || !section || 
                isblank(buf[indent_len]) != 0) {
                mk_config_error(path, line, "Invalid indentation level");
            }

            if (buf[indent_len] == '#' || indent_len == len) {
                continue;
            }

            /* Get key and val */
            i = mk_string_char_search(buf + indent_len, ' ', len - indent_len);
            key = mk_string_copy_substr(buf + indent_len, 0, i);
            val = mk_string_copy_substr(buf + indent_len + i, 1, len - indent_len);

            if (!key || !val || i < 0) {
                mk_config_error(path, line, "Each key must have a value");
                continue;
            }

            /* Trim strings */
            mk_string_trim(&key);
            mk_string_trim(&val);

            /* Register entry: key and val are copied as duplicated */
            mk_config_entry_add(conf, key, val);

            /* Free temporal key and val */
            mk_mem_free(key);
            mk_mem_free(val);
        }
    }

    /*
    struct mk_config_section *s;
    struct mk_config_entry *e;

    s = conf->section;
    while(s) {
        printf("\n[%s]", s->name);
        e = s->entry;
        while(e) {
            printf("\n   %s = %s", e->key, e->val);
            e = e->next;
        }
        s = s->next;
    }
    fflush(stdout);
    */

    fclose(f);
    return conf;
}

void mk_config_free(struct mk_config *conf)
{
    struct mk_config_section *prev=0, *section;

    /* Free sections */
    section = conf->section;
    while (section) {
        while (section->next) {
            prev = section;
            section = section->next;
        }
        /* Free section entries */
        mk_config_free_entries(section);

        /* Free section node */
        mk_mem_free(section->name);
        mk_mem_free(section);

        if (section == conf->section) {
            return;
        }
        prev->next = NULL;
        section = conf->section;
    }
}

void mk_config_free_entries(struct mk_config_section *section)
{
    struct mk_config_entry *prev = 0, *target;

    target = section->entry;
    while (target) {
        while (target->next) {
            prev = target;
            target = target->next;
        }

        /* Free memory assigned */
        mk_mem_free(target->key);
        mk_mem_free(target->val);

        if (target == section->entry) {
            section->entry = NULL;
            return;
        }

        prev->next = NULL;
        target = section->entry;
    }
}

void *mk_config_section_getval(struct mk_config_section *section, char *key, int mode)
{
    int on, off;
    struct mk_config_entry *entry;

    entry = section->entry;
    while (entry) {
        if (strcasecmp(entry->key, key) == 0) {
            switch (mode) {
            case MK_CONFIG_VAL_STR:
                return (void *) entry->val;
            case MK_CONFIG_VAL_NUM:
                return (void *) (size_t) strtol(entry->val, (char **) NULL, 10);
            case MK_CONFIG_VAL_BOOL:
                on = strcasecmp(entry->val, VALUE_ON);
                off = strcasecmp(entry->val, VALUE_OFF);

                if (on != 0 && off != 0) {
                    return (void *) -1;
                }
                else if (on >= 0) {
                    return (void *) MK_TRUE;
                }
                else {
                    return (void *) MK_FALSE;
                }
            case MK_CONFIG_VAL_LIST:
                return mk_string_split_line(entry->val);
            }
        }
        else {
            entry = entry->next;
        }
    }
    return NULL;
}

/* Read configuration files */
static void mk_config_read_files(char *path_conf, char *file_conf)
{
    unsigned long len;
    char *path = 0;
    struct stat checkdir;
    struct mk_config *cnf;
    struct mk_config_section *section;

    config->serverconf = mk_string_dup(path_conf);
    config->workers = MK_WORKERS_DEFAULT;

    if (stat(config->serverconf, &checkdir) == -1) {
        mk_err("ERROR: Cannot find/open '%s'", config->serverconf);
        exit(EXIT_FAILURE);
    }

    mk_string_build(&path, &len, "%s/%s", path_conf, file_conf);

    cnf = mk_config_create(path);
    if (!cnf) {
        mk_err("Cannot read 'monkey.conf'");
        exit(EXIT_FAILURE);
    }

    section = mk_config_section_get(cnf, "SERVER");

    if (!section) {
        mk_err("ERROR: No 'SERVER' section defined");
    }

    /* Map source configuration */
    config->config = cnf;

    /* Listen */
    config->listen_addr = mk_config_section_getval(section, "Listen", 
                                                   MK_CONFIG_VAL_STR);
    if (!config->listen_addr) {
        config->listen_addr = MK_DEFAULT_LISTEN_ADDR;
    }

    /* Connection port */
    config->serverport = (size_t) mk_config_section_getval(section,
                                                        "Port", 
                                                        MK_CONFIG_VAL_NUM);
    if (config->serverport <= 1 || config->serverport >= 65535) {
        mk_config_print_error_msg("Port", path);
    }

    /* Number of thread workers */
    config->workers = (size_t) mk_config_section_getval(section,
                                                     "Workers", 
                                                     MK_CONFIG_VAL_NUM);
    if (config->workers < 1) {
        mk_config_print_error_msg("Workers", path);
    }
    /* Get each worker clients capacity based on FDs system limits */
    config->worker_capacity = mk_server_worker_capacity(config->workers);

    /* Timeout */
    config->timeout = (size_t) mk_config_section_getval(section,
                                                     "Timeout", MK_CONFIG_VAL_NUM);
    if (config->timeout < 1) {
        mk_config_print_error_msg("Timeout", path);
    }
    
    /* KeepAlive */
    config->keep_alive = (size_t) mk_config_section_getval(section,
                                                        "KeepAlive",
                                                        MK_CONFIG_VAL_BOOL);
    if (config->keep_alive == MK_ERROR) {
        mk_config_print_error_msg("KeepAlive", path);
    }

    /* MaxKeepAliveRequest */
    config->max_keep_alive_request = (size_t)
        mk_config_section_getval(section,
                                 "MaxKeepAliveRequest",
                                 MK_CONFIG_VAL_NUM);
    
    if (config->max_keep_alive_request == 0) {
        mk_config_print_error_msg("MaxKeepAliveRequest", path);
    }

    /* KeepAliveTimeout */
    config->keep_alive_timeout = (size_t) mk_config_section_getval(section,
                                                                "KeepAliveTimeout",
                                                                MK_CONFIG_VAL_NUM);
    if (config->keep_alive_timeout == 0) {
        mk_config_print_error_msg("KeepAliveTimeout", path);
    }

    /* Pid File */
    config->pid_file_path = mk_config_section_getval(section,
                                                     "PidFile", MK_CONFIG_VAL_STR);
    
    /* Home user's directory /~ */
    config->user_dir = mk_config_section_getval(section, 
                                                "UserDir", MK_CONFIG_VAL_STR);

    /* Index files */
    config->index_files = mk_config_section_getval(section,
                                                   "Indexfile", MK_CONFIG_VAL_LIST);

    /* HideVersion Variable */
    config->hideversion = (size_t) mk_config_section_getval(section,
                                                         "HideVersion",
                                                         MK_CONFIG_VAL_BOOL);
    if (config->hideversion == MK_ERROR) {
        mk_config_print_error_msg("HideVersion", path);
    }

    /* User Variable */
    config->user = mk_config_section_getval(section, "User", MK_CONFIG_VAL_STR);

    /* Resume */
    config->resume = (size_t) mk_config_section_getval(section,
                                                    "Resume", MK_CONFIG_VAL_BOOL);
    if (config->resume == MK_ERROR) {
        mk_config_print_error_msg("Resume", path);
    }

    /* Max Request Size */
    config->max_request_size = (size_t) mk_config_section_getval(section,
                                                              "MaxRequestSize",
                                                              MK_CONFIG_VAL_NUM);
    if (config->max_request_size <= 0) {
        mk_config_print_error_msg("MaxRequestSize", path);
    }
    else {
        config->max_request_size *= 1024;
    }

    /* Symbolic Links */
    config->symlink = (size_t) mk_config_section_getval(section,
                                                     "SymLink", MK_CONFIG_VAL_BOOL);
    if (config->symlink == MK_ERROR) {
        mk_config_print_error_msg("SymLink", path);
    }

    /* Transport Layer plugin */
    config->transport_layer = mk_config_section_getval(section,
                                                       "TransportLayer",
                                                       MK_CONFIG_VAL_STR);
    mk_mem_free(path);
    mk_config_read_hosts(path_conf);
}

void mk_config_read_hosts(char *path)
{
    DIR *dir;
    unsigned long len;
    char *buf = 0;
    char *file;
    struct host *p_host, *new_host;     /* debug */
    struct dirent *ent;

    /* Read default virtual host file */
    mk_string_build(&buf, &len, "%s/sites/default", path);
    config->hosts = mk_config_get_host(buf);
    config->nhosts++;
    mk_mem_free(buf);
    buf = NULL;

    if (!config->hosts) {
        mk_err("Error parsing main configuration file 'default'");
    }

    /* Read all virtual hosts defined in sites/ */
    mk_string_build(&buf, &len, "%s/sites/", path);
    if (!(dir = opendir(buf))) {
        mk_err("Could not open %s", buf);
    }
    mk_mem_free(buf);

    p_host = config->hosts;

    /* Reading content */
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp((char *) ent->d_name, ".") == 0)
            continue;
        if (strcmp((char *) ent->d_name, "..") == 0)
            continue;
        if (strcasecmp((char *) ent->d_name, "default") == 0)
            continue;

        mk_string_build(&file, &len, "%s/sites/%s", path, ent->d_name);

        new_host = (struct host *) mk_config_get_host(file);
        mk_mem_free(file);
        if (!new_host) {
            continue;
        }
        else {
            p_host->next = new_host;
            p_host = new_host;
            config->nhosts++;
        }
    }
    closedir(dir);
}

struct host *mk_config_get_host(char *path)
{
    unsigned long len = 0;
    char *host_low;
    struct stat checkdir;
    struct host *host;
    struct host_alias *new_alias;
    struct mk_config *cnf;
    struct mk_config_section *section;
    struct mk_string_line *line, *line_p;

    /* Read configuration file */
    cnf = mk_config_create(path);

    /* Read tag 'HOST' */
    section = mk_config_section_get(cnf, "HOST");

    /* Alloc configuration node */
    host = mk_mem_malloc_z(sizeof(struct host));
    host->config = cnf;
    host->file = mk_string_dup(path);

    /* Alloc list for host name aliases */
    mk_list_init(&host->server_names); 

    host_low = mk_mem_malloc_z(MK_HOSTNAME_LEN);
    line_p = line = mk_config_section_getval(section, "Servername", MK_CONFIG_VAL_LIST);
    while (line_p) {
        if (line_p->len > MK_HOSTNAME_LEN - 1) {
            line_p = line_p->next;
            continue;
        }

        /* Hostname to lowercase */
        char *h = host_low;
        char *p = line_p->val;

        while (*p) {
            *h = tolower(*p);
            p++, h++;
        }
        *h = '\0';

        /* Alloc node */
        new_alias = mk_mem_malloc_z(sizeof(struct host_alias));
        new_alias->name = mk_mem_malloc_z(line_p->len + 1);
        strncpy(new_alias->name, host_low, line_p->len);

        new_alias->len = line_p->len;

        mk_list_add(&new_alias->_head, &host->server_names);

        line_p = line_p->next;
    }
    mk_mem_free(host_low);

    /* document root handled by a mk_pointer */
    host->documentroot.data = mk_config_section_getval(section,
                                                       "DocumentRoot",
                                                       MK_CONFIG_VAL_STR);
    host->documentroot.len = strlen(host->documentroot.data);

    /* validate document root configured */
    if (stat(host->documentroot.data, &checkdir) == -1) {
        mk_err("Invalid path to DocumentRoot in %s", path);
    }
    else if (!(checkdir.st_mode & S_IFDIR)) {
        mk_err("DocumentRoot variable in %s has an invalid directory path", path);
    }

    if (mk_list_is_empty(&host->server_names) == 0) {
        mk_config_free(cnf);
        return NULL;
    }

    /* Server Signature */
    if (config->hideversion == MK_FALSE) {
        mk_string_build(&host->host_signature, &len,
                        "Monkey/%s", VERSION);
    }
    else {
        mk_string_build(&host->host_signature, &len, "Monkey");
    }
    mk_string_build(&host->header_host_signature.data,
                    &host->header_host_signature.len,
                    "Server: %s", host->host_signature);

    host->next = NULL;
    return host;
}

void mk_config_set_init_values(void)
{
    /* Init values */
    config->is_seteuid = MK_FALSE;
    config->timeout = 15;
    config->hideversion = MK_FALSE;
    config->keep_alive = MK_TRUE;
    config->keep_alive_timeout = 15;
    config->max_keep_alive_request = 50;
    config->resume = MK_TRUE;
    config->standard_port = 80;
    config->listen_addr = MK_DEFAULT_LISTEN_ADDR;
    config->serverport = 2001;
    config->symlink = MK_FALSE;
    config->nhosts = 0;
    config->user = NULL;
    config->open_flags = O_RDONLY | O_NONBLOCK;
    config->index_files = NULL;
    config->user_dir = NULL;

    /* Max request buffer size allowed
     * right now, every chunk size is 4KB (4096 bytes),
     * so we are setting a maximum request size to 32 KB */
    config->max_request_size = MK_REQUEST_CHUNK * 8;

    /* Plugins */
    config->plugins = mk_mem_malloc(sizeof(struct mk_list));

    /* Internals */
    config->safe_event_write = MK_FALSE;

    /* 
     * Transport type: useful to build redirection headers, values:
     *
     *   MK_TRANSPORT_HTTP
     *   MK_TRANSPORT_HTTPS
     *
     * we set default to 'http'
     */
    config->transport = MK_TRANSPORT_HTTP;
    config->transport_layer = NULL;

    /* Init plugin list */
    mk_list_init(config->plugins);
}

/* read main configuration from monkey.conf */
void mk_config_start_configure(void)
{
    unsigned long len;

    mk_config_set_init_values();
    mk_config_read_files(config->file_config, M_DEFAULT_CONFIG_FILE);

    /* Load mimes */
    mk_mimetype_read_config();

    /* Basic server information */
    if (config->hideversion == MK_FALSE) {
        mk_string_build(&config->server_software.data,
                        &len, "Monkey/%s (%s)", VERSION, OS);
        config->server_software.len = len;
    }
    else {
        mk_string_build(&config->server_software.data, &len, "Monkey Server");
        config->server_software.len = len;
    }
}

int mk_config_host_find(mk_pointer host, struct host **vhost, struct host_alias **alias)
{
    struct host_alias *entry;
    struct mk_list *head;
    struct host *aux_host;

    aux_host = config->hosts;
    while (aux_host) {
        mk_list_foreach(head, &aux_host->server_names) {
            entry = mk_list_entry(head, struct host_alias, _head);
            if (entry->len == host.len &&
                strncmp(entry->name, host.data, host.len) == 0) {
                *vhost = aux_host;
                *alias = entry;
                return 0;
            }
        }
        aux_host = aux_host->next;
    }

    return -1;
}

void mk_config_sanity_check()
{
    /* Check O_NOATIME for current user, flag will just be used 
     * if running user is allowed to.
     */
    int fd, flags = config->open_flags;

    flags |= O_NOATIME;
    fd = open(config->file_config, flags);

    if (fd > -1) {
        config->open_flags = flags;
        close(fd);
    }
}