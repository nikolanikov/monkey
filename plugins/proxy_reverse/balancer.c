#include <stdint.h>
#include <regex.h>

#include "types.h"
#include "config.h"

static unsigned next = 0;

static unsigned next_shared = 0;
static pthread_mutex_t next_mutex = PTHREAD_MUTEX_INITIALIZER;

struct dict servers;
static pthread_mutex_t servers_mutex = PTHREAD_MUTEX_INITIALIZER;

struct server
{
	unsigned response_time; // response time in microseconds
	time_t offline;
	// TODO add what we need here
};

static char *format_uint(char *buffer, uint64_t number, uint16_t length)
{
	char *end = buffer + length, *position = end - 1;
	do *position-- = '0' + (number % 10);
	while (number /= 10);
	return end;
}
static uint16_t format_uint_length(uint64_t number)
{
	uint16_t length = 1;
	while (number /= 10) ++length;
	return length;
}

#define SERVER_KEY_SIZE_LIMIT (127 + 1 + 5)		/* domain:port */

static int key_init(struct string *key, const struct proxy_server_entry *entry)
{
	size_t length = format_uint_length(entry->port);
	key->length = strlen(entry->hostname);
	if ((key->length + 1 + length) > SERVER_KEY_SIZE_LIMIT) return -1; // invalid server entry

	memcpy(key->data, entry->hostname, key->length);
	key->data[key->length++] = ':';
	format_uint(key->data + key->length, entry->port, length);
	key->length += length;

	return 0;
}

int proxy_balance_init(const struct proxy_entry_array *config)
{
	if (!dict_init(&servers, DICT_SIZE_BASE)) return ERROR_MEMORY;

	size_t i, j;
	char buffer[SERVER_KEY_SIZE_LIMIT];
	struct string key;
	unsigned *value;
	int status;

	key.data = buffer;

	// Add entry for each slave server in the servers dictionary.
	for(i = 0; i < config->length; i++)
	{
		for(j = 0; j < config->entry[i].server_list->length; j++)
		{
			if (key_init(&key, config->entry[i].server_list->entry + j) < 0) return -2;

			value = malloc(sizeof(unsigned));
			if (!value) return ERROR_MEMORY; // memory error
			*value = 0;

			status = dict_add(&servers, &key, value); // dict_add will auto reject all repeated entries
			if (status == ERROR_MEMORY) return ERROR_MEMORY;
		}
	}

	// From here on, the only allowed modification of servers is to change the value of an item.
	// This allows certain performance optimizations to be done.

	return 0;
}

static int balance_connect(const struct proxy_server_entry *entry)
{
	int fd;
	//struct timeval before, after;

    //gettimeofday(&before, 0);
	fd = mk_api->socket_connect(entry->hostname, entry->port);
	//gettimeofday(&after, 0);

	if (fd >= 0)
	{
		// TODO store response time and some more data (if necessary)
		//unsigned response_time = (after.tv_sec - before.tv_sec) * 1000000 + after.tv_usec - before.tv_usec;

		mk_api->socket_set_nonblocking(fd);
	}

	return fd;
}

//client = time(0) % server_list->length;

// Simple, non-fair load balancer with almost no overhead.
int proxy_balance_naive(const struct session_request *sr, const struct proxy_server_entry_array *server_list, unsigned seed)
{
	size_t index;
	int fd;

	for(index = 0; index < server_list->length; ++index)
	{
		fd = balance_connect(server_list->entry + ((index + seed) % server_list->length));
		if (fd >= 0) return fd;
	}

	return -1;
}

// Simple load balancer with almost no overhead. Race conditions are possible under heavy load, but they will just lead to unfair sharing of the load.
int proxy_balance_rr_lockless(const struct session_request *sr, const struct proxy_server_entry_array *server_list)
{
	size_t index, from, to;
	int fd = -1;

	for(from = next, to = from + server_list->length; from < to; ++from)
	{
		index = from % server_list->length;
		fd = balance_connect(server_list->entry + index);
		if (fd >= 0)
		{
			next = index + 1; // remember which server handled the request
			break;
		}
	}

	return fd;
}

// Simple load balancer. Race conditions are prevented with mutexes. This adds significant overhead under heavy load.
int proxy_balance_rr_locking(const struct session_request *sr, const struct proxy_server_entry_array *server_list)
{
	size_t index, from, to;
	int fd = -1;

	pthread_mutex_lock(&next_mutex);

	for(from = next_shared, to = from + server_list->length; from < to; ++from)
	{
		index = from % server_list->length;
		fd = balance_connect(server_list->entry + index);
		if (fd >= 0)
		{
			next_shared = index + 1; // remember which server handled the request
			break;
		}
	}

	pthread_mutex_unlock(&next_mutex);
	return fd;
}

// Ensures equal load in most use cases. All servers are traversed to find the one with least connections. This adds significant overhead.
int proxy_balance_leastconnections(const struct session_request *sr, const struct proxy_server_entry_array *server_list, void **connection)
{
	int fd;

	size_t index, index_min;
	unsigned *count, *count_min;

	char buffer[SERVER_KEY_SIZE_LIMIT];
	struct string key;

	key.data = buffer;

	if (key_init(&key, server_list->entry) < 0) return -2;
	count_min = dict_get(&servers, &key);
	index_min = 0;

	pthread_mutex_lock(&servers_mutex);

	for(index = 1; index < server_list->length; ++index)
	{
		if (key_init(&key, server_list->entry + index) < 0) return -2;
		count = dict_get(&servers, &key);
		if (*count < *count_min)
		{
			count_min = count;
			index_min = index;
		}
	}

	fd = balance_connect(server_list->entry + index_min);
	if (fd >= 0) *count_min += 1;

	pthread_mutex_unlock(&servers_mutex);

	key_init(&key, server_list->entry + index_min);
	*connection = string_alloc(key.data, key.length);

	return fd;
}

void proxy_balance_close(void *connection)
{
	unsigned *count = dict_get(&servers, connection);

	pthread_mutex_lock(&servers_mutex);
	*count -= 1;
	pthread_mutex_unlock(&servers_mutex);

	free(connection);
}

//
//Weighted Round Robin
// WRR will act exactly like round robin, because of the same weights of the connections.
// This way the implementation will be simple and fast.
// For example if we have 2 hosts host1 with weight 3 and host2 with weight 1
// I will make an array with 4 pointers 3 to host 1 and 1 to host2
// By making RR on the array, host1 will be invoked 3 times more
//
/*int proxy_balance_rr_weighted(const struct session_request *sr, const struct proxy_server_entry_array *server_list)
{
	pthread_mutex_lock(&next_mutex);
	size_t from = next_shared + 1, to = from + server_list->length;
	const struct proxy_server_entry *server;
	int fd;

	// assert(server_list->length);
	do
	{
		pthread_mutex_unlock(&next_mutex);
		server = server_list->entry + (from % server_list->length);
		fd = mk_api->socket_connect(server->hostname, server->port);
		if (fd >= 0)
		{
			next_shared = from;
			mk_api->socket_set_nonblocking(fd);
			return fd;
		}
		pthread_mutex_lock(&next_mutex);
	} while (++from < to);

	pthread_mutex_unlock(&next_mutex);
	return -1;
}
*/

/*
char *server_list[] = {"host1","host2","host3"}
char *test_balance(...)
{
return [pr_loadbalance_sport_based(...)];

}
*/
