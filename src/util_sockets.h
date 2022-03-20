#ifndef UTIL_SOCKETS_H_GUARD_
#define UTIL_SOCKETS_H_GUARD_
    
typedef struct util_socket_ *util_socket_s;
typedef struct util_socket_addr_ *util_socket_addr_s;
typedef enum
{
    UTIL_SOCKET_PROT_DATAGRAM = 0,
    UTIL_SOCKET_PROT_STREAM = 1
}
util_socket_prot_e;

typedef enum
{
    UTIL_SOCKET_ERROR_NO_RESOURCES = -4,
    UTIL_SOCKET_ERROR_DROPPED = -3,
    UTIL_SOCKET_ERROR_TIMEOUT = -2,
    UTIL_SOCKET_ERROR_UNKNOWN = -1,
    UTIL_SOCKET_ERROR_NO_ERROR = 0,
    UTIL_SOCKET_ERROR_NO_MSG = 1
}
util_socket_error_e;

#define UTIL_SOCKET_MAX_CONNECTIONS 2048

util_socket_s util_socket_new(util_socket_prot_e);
void util_socket_destroy(util_socket_s);
int util_socket_get_port(util_socket_s);
int util_socket_set_non_blocking(int non_blocking); // created sockets afterwards will be non_blocking
int util_socket_set_number_connections(int number_connections);
util_socket_addr_s util_socket_addr_new();
int util_socket_addr_get(util_socket_addr_s, util_socket_s);
int util_socket_addr_is_equals(util_socket_addr_s a1, util_socket_addr_s a2); // returns 1 if is equals
int util_socket_addr_cpy(util_socket_addr_s dst, util_socket_addr_s src);
int util_socket_addr_get_name(util_socket_addr_s addr, int *port, char *buffer, int size_buffer);
int util_socket_addr_destroy(util_socket_addr_s);
int util_socket_shutdown(util_socket_s s); // unblocks socket
int util_socket_bind(util_socket_s, const char *hostname, long port);
int util_socket_connect_sa(util_socket_s, util_socket_addr_s sa);
int util_socket_connect(util_socket_s, const char *hostname, long port, util_socket_addr_s sa);
int util_socket_listen(util_socket_s); // for stream
util_socket_s util_socket_accept(util_socket_s, util_socket_addr_s sa_remote); // for stream (returns new connected socket)
// returns -1 on error, 1 on non-blocking and no message, 0 on no error
util_socket_error_e util_socket_recv(util_socket_s, util_socket_addr_s, void *buffer, int buffer_size, int *recv_size);
util_socket_error_e util_socket_send(util_socket_s, void *buffer, int size);


#endif /* UTIL_SOCKETS_H_GUARD_ */
