#ifndef CONNECTLIB_H_GUARD
#define CONNECTLIB_H_GUARD

#define CONN_MAX_BACKOFF_NS    7e6      // in nanoseconds (7ms)
#define CONN_BUFFER_SIZE       1048576  // in Bytes
#define CONN_NAME_BUFFER_SIZE  256      // in Bytes

typedef struct conn_server_* conn_server_s;
typedef struct conn_*        conn_s;

typedef struct conn_peer_
{
	char   name[CONN_NAME_BUFFER_SIZE];
	int    port;
	double time_since_ms; // how much time since last connection
}*
conn_peer_s;

typedef enum {
	CONN_ERR_UNKNOWN = -1,
	CONN_ERR_TIMEOUT = -2,
	CONN_ERR_DROPPED = -3, // connection dropped
	CONN_ERR_BUFFERS = -4, // no space
}
conn_error_type_e;

typedef struct conn_server_bindings_
{
	// after the server is ready to receive messages
	void (*on_ready)(void *args);
	void (*on_stop)(conn_peer_s, conn_s, double time_elapsed_ms, void *args);

	// called while messages do not come in (leave it empty)
	// the time between calls increases exponentially up to 
	// CONN_MAX_BACKOFF
	void (*on_waiting)(conn_peer_s, conn_s, double time_elapsed_ms, void *args); // polls all peers (can be used to implement is alive)

	// when a message is received
	void (*on_recv)(conn_peer_s, conn_s, void *buffer, long size, void *args);
	void (*on_error)(conn_peer_s, conn_error_type_e, void *args);
}*
conn_server_bindings_s;

typedef struct conn_bindings_
{
	void (*on_ready)(void *args);
	void (*on_disconnect)(conn_peer_s, conn_s, double time_elapsed_ms, void *args);
	void (*on_waiting)(conn_peer_s, conn_s, double time_elapsed_ms, void *args);
	void (*on_recv)(conn_peer_s, conn_s, void *buffer, long size, void *args);
	void (*on_error)(conn_peer_s, conn_error_type_e, void *args);
}*
conn_bindings_s;

typedef enum {
	CONN_UDP,
	CONN_TCP
}
conn_server_type_e;

void conn_init(int nbOnRecvThrs); // TODO: handle main args
void conn_exit();

// the port is truncated at 16 bits
// one can pass arguments into the server or on connection
conn_server_s conn_server_start(conn_server_bindings_s,      long port, conn_server_type_e,           void *args);
int        conn_server_get_port(         conn_server_s);
// in case of CONN_TCP, conn_connect will block until the connection can be established 
conn_s        conn_connect     ( const char *host_name,      long port, conn_server_type_e, conn_bindings_s recv, void *args);
int           conn_send        (                conn_s,   void *buffer,          long size);
int           conn_disconnect  (                conn_s);
int           conn_server_stop (         conn_server_s);

#endif // CONNECTLIB_H_GUARD