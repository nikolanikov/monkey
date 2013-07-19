#include <regex.h>
#include <stdio.h>
#include <time.h>

#include "types.h"
#include "config.h"

static unsigned last = 0;

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

//
// Current load balancer returns host depending on the file descriptor id
// It is really fast and lockless. 
// in most of the cases first server will be mostly invoked
// 
// In low loaded environments the load balancer will act as follows.
// When there are a single connection it will be forwarded to the first server.
// if second connection arrives while the first is still active it will be forwarded to the next server.
//
int proxy_balance_fdid_based(int client, const struct session_request *sr, const struct proxy_server_entry_array *server_list)
{
	size_t index;
	const struct proxy_server_entry *server;
	int fd;

	// assert(server_list->length);

	// TODO maybe use current time instead of socket
	client = time(0) % server_list->length;

	for(index = 0; index < server_list->length; ++index)
	{
		server = server_list->entry + ((index + client) % server_list->length);
		fd = mk_api->socket_connect(server->hostname, server->port);
		if (fd >= 0)
		{
			mk_api->socket_set_nonblocking(fd);
			return fd;
		}
	}

	return -1;
}

//
// First Alive
// This is simplest implementation of first alive and will be available after the heartbeat is done
//

int pr_loadbalance_first_alive(int last,int max) // -1 as initial value
{

if(last==max)last=-1;
//todo to increase max until finds server that is not turned off
return last+1;
}

//
//Lockless Round Robin
//On high loaded environments it won't be so accurate, if 2 threads reach x simultaneously,
//both will connect to the same server, and the next thread will connect to server + 1
// for low loaded environments will be really fast, because it is not usings locks.

//int x=0; // this must not be defined here, is just for test
//int pr_loadbalance_rr_lockless(unsigned short sock_fd,int max)
int proxy_balance_rr_lockless(const struct session_request *sr, const struct proxy_server_entry_array *server_list)
{
	size_t from = last + 1, to = from + server_list->length;
	const struct proxy_server_entry *server;
	int fd;

	// assert(server_list->length);

	do
	{
		server = server_list->entry + (from % server_list->length);
		fd = mk_api->socket_connect(server->hostname, server->port);
		if (fd >= 0)
		{
			last = from;
			mk_api->socket_set_nonblocking(fd);
			return fd;
		}
	} while (++from < to);

	return -1;

return x++ % max;
}

//
//Weighted Round Robin
// WRR will act exactly like round robin, because of the same weights of the connections.
// This way the implementation will be simple and fast.
// For example if we have 2 hosts host1 with weight 3 and host2 with weight 1
// I will make an array with 4 pointers 3 to host 1 and 1 to host2
// By making RR on the array, host1 will be invoked 3 times more
//



/*
char *server_list[] = {"host1","host2","host3"}
char *test_balance(...)
{
return [pr_loadbalance_sport_based(...)];

}
*/
