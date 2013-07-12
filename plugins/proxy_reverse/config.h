//TODO some of the structures are using C99

enum balancer_type
{
	Hash=1,
	FirstAlive,
	RoundRobin,
	WRoundRobin
};

/*
struct balancer
{
	enum balancer_type type;
	union
	{
		struct hash
		{
			// ...
		} hash;
		struct first_alive
		{
			// ...
		} first_alive;
		//...
	} params;
};
*/

struct proxy_server_entry
{
	char *hostname;
	int port;
};

struct proxy_server_entry_array
{
	unsigned int length;
	struct proxy_server_entry entry[];
};

struct match_regex_array
{
	unsigned int length;
	regex_t entry[];
};

struct proxy_cnf_default_values
{
	struct proxy_server_entry_array *server_list;
	enum balancer_type balancer_type;
};

struct proxy_entry
{
	struct proxy_server_entry_array *server_list;
	enum balancer_type balancer_type;
	struct match_regex_array *regex_array;
};

struct proxy_entry_array
{
	unsigned int length;
	struct proxy_entry entry[];
};

struct proxy_entry_array *proxy_reverse_read_config(const char *);

struct proxy_entry *proxy_check_match(char *, struct proxy_entry_array *);
