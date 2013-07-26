// Naive
// Connects to the first alive server, starting from server.
int proxy_balance_naive(const struct session_request *sr, const struct proxy_server_entry_array *server_list, unsigned seed);

// First-Alive
// Connects to the first alive server, starting from 0.
#define proxy_balance_firstalive(sr, server_list) proxy_balance_naive((sr), (server_list), 0)

// Lockless Round Robin
// Each consecutive call connects to the next available server. Race conditions can occur since no locking is performed.
int proxy_balance_rr_lockless(const struct session_request *sr, const struct proxy_server_entry_array *server_list);

// Locking Round Robin
// Each consecutive call connects to the next available server. Race conditions are prevented at the expense of performance.
int proxy_balance_rr_locking(const struct session_request *sr, const struct proxy_server_entry_array *server_list);

// Least connections
// Connects to the server with the least number of connections. Ensures equal load in most use cases but adds significant overhead.
int proxy_balance_leastconnections(const struct session_request *sr, const struct proxy_server_entry_array *server_list, void **connection);

void proxy_balance_close(void *connection);
