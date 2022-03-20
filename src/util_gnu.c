#define GNU_SOURCE

#include <time.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <fcntl.h>
#include <threads.h>
#include <semaphore.h>
#include <stdatomic.h>

#include "util_sockets.h"
#include "util_time.h"
#include "util_sync.h"

#define BUFFER_SIZE 128
#define PORT_MASK 0xFFFF
#define CHECK_PORT(_port) (_port == -1 ? 0x7FFF : _port & PORT_MASK)
#define REPORT_ERROR fprintf(stderr, "%s:%d %s\n", __func__, __LINE__, strerror(errno))

static int bind_addr(const char *hostname, long p, struct sockaddr_in *si);

struct util_socket_
{
	int socket;
	util_socket_prot_e prot;
	struct sockaddr_in si_local;
	struct sockaddr sa_remote;
};

struct util_socket_addr_
{
	struct sockaddr_in si;
};

struct util_thrd_args_
{
	int id;
	_Atomic int is_stop;
	util_thrd_s thrd;
};

struct util_thrd_ 
{
	sem_t empty;
	sem_t full;
	_Atomic unsigned long in;
	_Atomic unsigned long out;
	int nb_workers;
	thrd_t *workers;
	struct util_thrd_args_ *workers_args;
	struct util_thrd_work_ volatile buffer[UTIL_SYNC_QUEUE_SIZE];
	// pthread_mutex_t mutex;
};

static clockid_t clock_type;
static int is_non_blocking_socket = 0;
static int number_connections = 1;

int
util_clock_set_type(
	clock_type_e clockType
) {
	switch (clockType)
	{
	case UTIL_CLOCK_FASTER:
		clock_type = CLOCK_THREAD_CPUTIME_ID;
		break;
	case UTIL_CLOCK_TODO:
		clock_type = CLOCK_THREAD_CPUTIME_ID; // TODO
		break;
	default:
		clock_type = CLOCK_THREAD_CPUTIME_ID; // TODO
		break;
	}
	clock_type = clockType;
	return 0;
}

int
util_clock_gettime(
	struct timespec *tp
) {
	return clock_gettime(clock_type, tp);
}

util_socket_s
util_socket_new(
	util_socket_prot_e prot
) {
	util_socket_s ret = (util_socket_s)malloc(sizeof(struct util_socket_));
	assert(ret != NULL && "malloc failed");
	ret->socket = -1;
	ret->prot = prot;
	int p = -1;
	int t = -1;
	if (prot == UTIL_SOCKET_PROT_DATAGRAM)
	{
		p = IPPROTO_UDP;
		t = SOCK_DGRAM;
	}
	else if (prot == UTIL_SOCKET_PROT_STREAM)
	{
		p = IPPROTO_TCP;
		t = SOCK_STREAM;
	}
	ret->socket = socket(AF_INET, t, p);
	if (-1 == ret->socket)
	{
	error_socket:
		REPORT_ERROR;
		free(ret);
		return NULL;
	}
	if (is_non_blocking_socket && prot == UTIL_SOCKET_PROT_DATAGRAM)
	{
		if (-1 == fcntl(ret->socket, F_SETFL, fcntl(ret->socket, F_GETFL, 0) | O_NONBLOCK))
		{
			goto error_socket;
		}
	}
	return ret;
}

int
util_socket_get_port(
	util_socket_s s
) {
	struct sockaddr_in si;
	socklen_t slen = sizeof(si);
	getsockname(s->socket, (struct sockaddr *)&si, &slen);
	return htons(si.sin_port);
}

void
util_socket_destroy(
	util_socket_s s
) {
	close(s->socket);
	free(s);
}

int
util_socket_shutdown(
	util_socket_s s
) {
	return shutdown(s->socket, SHUT_RDWR);
}

int
util_socket_set_non_blocking(
	int non_blocking
) {
	is_non_blocking_socket = non_blocking;
	return 0;
}

int
util_socket_set_number_connections(
	int numberConnections
) {
	number_connections = numberConnections;
}

util_socket_addr_s
util_socket_addr_new()
{
	util_socket_addr_s ret;
	ret = (util_socket_addr_s)calloc(1, sizeof(struct util_socket_addr_));
	return ret;
}

int
util_socket_addr_get(
	util_socket_addr_s addr,
	util_socket_s sckt
) {
	memcpy(&addr->si, &sckt->si_local, sizeof(addr->si));
	return 0;
}

int
util_socket_addr_is_equals(
	util_socket_addr_s a1,
	util_socket_addr_s a2
) {
	return a2->si.sin_addr.s_addr == a1->si.sin_addr.s_addr && a2->si.sin_port == a1->si.sin_port;
}

int
util_socket_addr_cpy(
	util_socket_addr_s dst,
	util_socket_addr_s src
) {
	memcpy(&dst->si, &src->si, sizeof(dst->si));
	return 0;
}

int
util_socket_addr_get_name(
	util_socket_addr_s addr,
	int *port,
	char *buffer,
	int size_buffer
) {
	socklen_t slen = sizeof(addr->si);
	*buffer = '\0';
	getnameinfo(
		(const struct sockaddr *)&addr->si,
		slen,
		buffer,
		size_buffer,
		NULL,
		0,
		NI_NAMEREQD);
	*(buffer + size_buffer - 1) = '\0';
	*port = htons(addr->si.sin_port);
	return 0;
}

int
util_socket_addr_destroy(
	util_socket_addr_s sa
) {
	free(sa);
	return 0;
}

int
util_socket_bind(
	util_socket_s sckt,
	const char *hostname,
	long p
) {
	uint16_t port = CHECK_PORT(p);
	int err;
	if (bind_addr(hostname, (long)port, &(sckt->si_local)))
		return -1;
	while (-1 == (err = bind(sckt->socket, (struct sockaddr *)&sckt->si_local, sizeof(sckt->si_local))) || p == -1)
	{
		if (err != -1)
			break;
		if (p == -1)
			port++;
		if (hostname || port < 1000 || p != -1)
		{
			close(sckt->socket);
			REPORT_ERROR;
			free(sckt);
			return -1;
		}
		sckt->si_local.sin_port = htons(port);
	}
	return 0;
}

int
util_socket_connect_sa(
	util_socket_s sckt,
	util_socket_addr_s sa
) {
	memcpy(&sckt->sa_remote, &sa->si, sizeof(sa->si));
	if (UTIL_SOCKET_PROT_DATAGRAM == sckt->prot)
		return 0; // no connection needed
	int flag = 1;
	if (-1 == setsockopt(sckt->socket, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int)))
	{
		REPORT_ERROR;
	}
	flag = 1;
	if (-1 == setsockopt(sckt->socket, IPPROTO_TCP, TCP_QUICKACK, (char *)&flag, sizeof(int)))
	{
		REPORT_ERROR;
	}
	if (connect(sckt->socket, (struct sockaddr*)&(sa->si), sizeof(sa->si)))
	{
		REPORT_ERROR;
		return -1;
	}
	if (is_non_blocking_socket && sckt->prot == UTIL_SOCKET_PROT_STREAM)
	{
		if (-1 == fcntl(sckt->socket, F_SETFL, fcntl(sckt->socket, F_GETFL, 0) | O_NONBLOCK))
		{
			REPORT_ERROR;
			return -1;
		}
	}
	return 0;
}

int
util_socket_connect(
	util_socket_s sckt,
	const char *hostname,
	long p,
	util_socket_addr_s sa
) {
	uint16_t port = CHECK_PORT(p);
	if (bind_addr(hostname, port, (struct sockaddr_in *)&(sckt->sa_remote)))
		return -1;
	if (UTIL_SOCKET_PROT_DATAGRAM == sckt->prot)
		return 0; // no connection needed

	int flag = 1;
	if (-1 == setsockopt(sckt->socket, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int)))
	{
		REPORT_ERROR;
	}
	flag = 1;
	if (-1 == setsockopt(sckt->socket, IPPROTO_TCP, TCP_QUICKACK, (char *)&flag, sizeof(int)))
	{
		REPORT_ERROR;
	}
	if (connect(sckt->socket, &(sckt->sa_remote), sizeof(sckt->sa_remote)))
	{
		REPORT_ERROR;
		return -1;
	}
	memcpy(&sa->si, &sckt->sa_remote, sizeof(sckt->sa_remote));
	if (is_non_blocking_socket && sckt->prot == UTIL_SOCKET_PROT_STREAM)
	{
		if (-1 == fcntl(sckt->socket, F_SETFL, fcntl(sckt->socket, F_GETFL, 0) | O_NONBLOCK))
		{
			REPORT_ERROR;
			return -1;
		}
	}
	return 0;
}

util_socket_error_e
util_socket_listen(
	util_socket_s sckt
) {
	if (UTIL_SOCKET_PROT_STREAM == sckt->prot)
	{
		int flag = 1;
		if (-1 == setsockopt(sckt->socket, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int)))
		{
			REPORT_ERROR;
		}
		flag = 1;
		if (-1 == setsockopt(sckt->socket, IPPROTO_TCP, TCP_QUICKACK, (char *)&flag, sizeof(int)))
		{
			REPORT_ERROR;
		}
	}
	if (-1 == listen(sckt->socket, UTIL_SOCKET_MAX_CONNECTIONS))
	{
		if (errno == ENOBUFS)
		{
			return UTIL_SOCKET_ERROR_NO_RESOURCES;
		}
		else if (errno == EINVAL)
		{
			return UTIL_SOCKET_ERROR_DROPPED;
		}
		else
		{
			return UTIL_SOCKET_ERROR_UNKNOWN;
		}
		close(sckt->socket);
	}
	if (is_non_blocking_socket && UTIL_SOCKET_PROT_STREAM == sckt->prot)
	{
		if (-1 == fcntl(sckt->socket, F_SETFL, fcntl(sckt->socket, F_GETFL, 0) | O_NONBLOCK))
		{
			return UTIL_SOCKET_ERROR_UNKNOWN;
		}
	}
	return UTIL_SOCKET_ERROR_NO_ERROR;
}

util_socket_s
util_socket_accept(
	util_socket_s sckt,
	util_socket_addr_s sa_remote
) {
	if (!(sckt->prot == UTIL_SOCKET_PROT_STREAM) || -1 == sckt->socket)
		return NULL;
	util_socket_s ret = (util_socket_s)malloc(sizeof(struct util_socket_));
	int flag = 1;
	assert(ret != NULL && "malloc failed");
	ret->socket = -1;
	ret->prot = sckt->prot;
	socklen_t socklen = sizeof(ret->sa_remote);
	memcpy(&ret->si_local, &sckt->si_local, socklen);
	
	flag = 1;
	if (-1 == setsockopt(sckt->socket, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int)))
	{
		REPORT_ERROR;
	}
	
	flag = 1;
	if (-1 == setsockopt(sckt->socket, IPPROTO_TCP, TCP_QUICKACK, (char *)&flag, sizeof(int)))
	{
		REPORT_ERROR;
	}

	int error = 0;
	socklen_t len = sizeof (error);
	getsockopt(sckt->socket, SOL_SOCKET, SO_ERROR, &error, &len);

	if (error != 0)
	{
		shutdown(sckt->socket, SHUT_RDWR);
		close(sckt->socket);
		sckt->socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (is_non_blocking_socket)
			fcntl(sckt->socket, F_SETFL, fcntl(sckt->socket, F_GETFL, 0) | O_NONBLOCK);
		bind(sckt->socket, (struct sockaddr *)&sckt->si_local, sizeof(sckt->si_local));
		listen(sckt->socket, UTIL_SOCKET_MAX_CONNECTIONS);
	}

	ret->socket = accept(sckt->socket, &ret->sa_remote, &socklen);
	
	assert(sizeof(ret->sa_remote) == socklen && "wrong address size");
	if (-1 == ret->socket)
	{
	error_socket:
		free(ret);
		if (EAGAIN != errno && EWOULDBLOCK != errno)
		{
			REPORT_ERROR;
		}
		return NULL;
	}
	else
	{
		flag = 1;
		setsockopt(ret->socket, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int));
		flag = 1;
		setsockopt(ret->socket, IPPROTO_TCP, TCP_QUICKACK, (char *)&flag, sizeof(int));
		if (is_non_blocking_socket)
			fcntl(ret->socket, F_SETFL, fcntl(ret->socket, F_GETFL, 0) | O_NONBLOCK);
	}
	if (sa_remote)
		memcpy(&sa_remote->si, &ret->sa_remote, sizeof(ret->sa_remote));
	return ret;
}

util_socket_error_e
util_socket_recv(
	util_socket_s s,
	util_socket_addr_s sa,
	void *buffer,
	int buffer_size,
	int *recv_size
) {
	assert(buffer != NULL && buffer_size > 0);
	assert(recv_size != NULL);
	assert(sa != NULL);
	assert(s != NULL);
	int slen = sizeof(s->si_local);
	int err = -1;

	if (s->prot == UTIL_SOCKET_PROT_DATAGRAM)
	{
		err = *recv_size = recvfrom(s->socket, buffer, buffer_size, 0, (struct sockaddr*)&sa->si, &slen);
	}

	if (s->prot == UTIL_SOCKET_PROT_STREAM)
	{
		err = *recv_size = recv(s->socket, buffer, buffer_size, 0);
	}

	if (-1 == err)
	{
		if (errno != EAGAIN && errno != EWOULDBLOCK)
		{
			if (errno == ETIMEDOUT)
			{
				return UTIL_SOCKET_ERROR_TIMEOUT;
			}
			else if (errno == ECONNRESET)
			{
				return UTIL_SOCKET_ERROR_TIMEOUT;
			}
			else if (errno == ENOTCONN || errno == ECONNREFUSED)
			{
				// TODO
				return UTIL_SOCKET_ERROR_NO_MSG;
			}
			else
			{
				REPORT_ERROR;
				return UTIL_SOCKET_ERROR_UNKNOWN;
			}
		}
		else
		{
			return UTIL_SOCKET_ERROR_NO_MSG;
		}
	}
	return UTIL_SOCKET_ERROR_NO_ERROR;
}

util_socket_error_e
util_socket_send(
	util_socket_s s,
	void *buffer,
	int buffer_size
) {
	assert(s != NULL);
	assert(buffer != NULL && buffer_size > 0);

	socklen_t slen = sizeof(s->sa_remote);
	int err = -1;
	if (s->prot == UTIL_SOCKET_PROT_DATAGRAM)
	{
		err = sendto(s->socket, buffer, buffer_size, 0, &s->sa_remote, slen);
	}

	if (s->prot == UTIL_SOCKET_PROT_STREAM)
	{
		err = send(s->socket, buffer, (size_t)buffer_size, MSG_NOSIGNAL);
	}

	if (-1 == err)
	{
		if (errno == ETIMEDOUT)
		{
			return UTIL_SOCKET_ERROR_TIMEOUT;
		}
		else if (errno == ECONNRESET)
		{
			return UTIL_SOCKET_ERROR_TIMEOUT;
		}
		else if (errno == ENOTCONN || errno == EPIPE)
		{
			// reconnect and try again
			if (s->prot == UTIL_SOCKET_PROT_STREAM)
			{
				shutdown(s->socket, SHUT_RDWR);
				close(s->socket);
				s->socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
				connect(s->socket, (struct sockaddr*)&(s->sa_remote), sizeof(s->sa_remote));
				if (is_non_blocking_socket)
					fcntl(s->socket, F_SETFL, fcntl(s->socket, F_GETFL, 0) | O_NONBLOCK);
			}
			return UTIL_SOCKET_ERROR_NO_MSG;
		}
		else
		{
			REPORT_ERROR;
			return UTIL_SOCKET_ERROR_UNKNOWN;
		}
	}
	return UTIL_SOCKET_ERROR_NO_ERROR;
}

static int
bind_addr(
	const char *hostname,
	long p,
	struct sockaddr_in *si
) {
	static const char *DEFAULT = "localhost";
	uint16_t port = CHECK_PORT(p);
	si->sin_family = AF_INET;
	si->sin_port = htons(port);
	struct addrinfo *res;
	int error;
	if (hostname)
	{
		error = getaddrinfo(hostname, NULL, NULL, &res);
	}
	else
	{
		error = getaddrinfo(DEFAULT, NULL, NULL, &res);
	}
	if (error)
	{
		fprintf(stderr, "error in getaddrinfo: %s\n", gai_strerror(error));
		return -1;
	}
	si->sin_addr.s_addr = ((struct sockaddr_in *)(res->ai_addr))->sin_addr.s_addr;
	freeaddrinfo(res);
	return 0;
}

static void util_thrd_empty(void *args) { }
static int
util_thrd_worker(
	void *args
) {
	struct util_thrd_args_ *a = (struct util_thrd_args_*)args;
	while(!atomic_load_explicit(&a->is_stop, memory_order_acquire))
	{
		sem_wait(&a->thrd->full);
		_Atomic unsigned long out = atomic_fetch_add(&a->thrd->out, 1);
		struct util_thrd_work_ volatile *item = &(a->thrd->buffer[out&UTIL_SYNC_QUEUE_MASK]);
		atomic_thread_fence(memory_order_acquire);
		if (item->callback)
		{
			item->callback(item->args);
			item->callback = NULL;
		}
		sem_post(&a->thrd->empty);
	}
	return 0;
}

static void thrd_push_work(util_thrd_s thrd, util_thrd_work_s work)
{   
	sem_wait(&thrd->empty);
	_Atomic unsigned long in = atomic_fetch_add(&thrd->in, 1);
	memcpy((void*)&(thrd->buffer[in&UTIL_SYNC_QUEUE_MASK]), work, sizeof(struct util_thrd_work_));
	atomic_thread_fence(memory_order_release);
	sem_post(&thrd->full);

}

util_thrd_s 
util_thrd_start_pool(
	int nb_thrs
) {
	util_thrd_s ret = (util_thrd_s)calloc(1, sizeof(struct util_thrd_));
	ret->nb_workers = nb_thrs;
	if (nb_thrs)
	{
		ret->workers = (thrd_t*)malloc(sizeof(thrd_t)*nb_thrs);
		ret->workers_args = (struct util_thrd_args_*)malloc(sizeof(struct util_thrd_args_)*nb_thrs);
	}
	ret->in = ret->out = 0;
	sem_init(&ret->empty, 0, UTIL_SYNC_QUEUE_SIZE-nb_thrs);
	sem_init(&ret->full, 0, 0);

	for(int i = 0; i < nb_thrs; i++) 
	{
		ret->workers_args[i].id = i;
		ret->workers_args[i].is_stop = 0;
		ret->workers_args[i].thrd = ret;
		thrd_create(ret->workers + i, util_thrd_worker, (void *)(ret->workers_args + i));
	}

	return ret;
}

void
util_thrd_stop_pool(
	util_thrd_s thrds
) {
	int ret;

	// set stop
	for(int i = 0; i < thrds->nb_workers; i++) 
		atomic_store_explicit(&thrds->workers_args[i].is_stop, 1, memory_order_release);

	// unblock
	for(int i = 0; i < thrds->nb_workers; i++) 
	{
		struct util_thrd_work_ work;
		work.callback = util_thrd_empty;
		work.args = NULL;
		thrd_push_work(thrds, &work);
	}

	// join
	for(int i = 0; i < thrds->nb_workers; i++)
		thrd_join(thrds->workers[i], &ret);

	sem_destroy(&thrds->empty);
	sem_destroy(&thrds->full);
	if (thrds->nb_workers)
	{
		free(thrds->workers_args);
		free(thrds->workers);
	}
	free(thrds);
}


// it does block if queue is full!
// work item is memcpy'ed to queue
void
util_thrd_push_work(
	util_thrd_s thrd,
	util_thrd_work_s work
) {
	if (thrd->nb_workers)
		thrd_push_work(thrd, work);
	else
		work->callback(work->args);
}
