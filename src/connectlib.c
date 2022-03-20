#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdatomic.h>
#include <time.h>
#include <threads.h>
#include <errno.h>

#include "util_sockets.h"
#include "util_time.h"
#include "util_sync.h"
#include "connectlib.h"

#define BUFFER_SIZE     128
#define REPORT_ERROR fprintf(stderr, "%s:%d %s\n", __func__, __LINE__, strerror(errno))

struct server_args_
{
	struct conn_server_bindings_ bindings;
	util_socket_s socket;
	int is_server;
	_Atomic int *isStop;
	_Atomic int *isReady;
	long nbPeers;
	struct conn_peer_i_ *peers;
	void *args;
	util_socket_addr_s sa;
};

struct conn_
{
	_Atomic int isStop;
	_Atomic int isReady;
	util_socket_s socket;
	util_socket_addr_s sa;
	thrd_t thread;
};

struct conn_peer_i_
{
	util_socket_s socket;
	util_socket_addr_s sa;
	conn_server_type_e t;
	double last_time;
	struct timespec lastTs;
	struct conn_peer_i_ *next;
};

struct conn_server_
{
	thrd_t thread;
	_Atomic int isStop;
	_Atomic int isReady;
	util_socket_s socket;
	conn_server_type_e s_type;
};

struct on_recv_args_
{
	void (*on_recv)(conn_peer_s, conn_s, void *buffer, long size, void *);
	struct conn_peer_ p;
	struct conn_ c;
	void *buf;
	long b_size;
	void *args;
	_Atomic int *is_stop;
};

#define TIMEOUT_MS 300000 // 5 min

static util_thrd_s thrds;

static double
diff_timespec_ms(
	struct timespec *t0,
	struct timespec *t1
) {
	double res = (t1->tv_sec - t0->tv_sec) * 1.0e3 + (t1->tv_nsec - t0->tv_nsec) * 1.0e-6;
	return res;
}

static void
cpy_timespec(
	struct timespec *dst,
	struct timespec *src
) {
	dst->tv_sec = src->tv_sec; // or just memcpy
	dst->tv_nsec = src->tv_nsec;
}

static void
i_peer_decode(
	struct conn_peer_i_ *p,
	conn_peer_s out
) {
	struct timespec now;
	util_clock_gettime(&now);
	double diff = diff_timespec_ms(&p->lastTs, &now);
	out->time_since_ms = diff;
	util_socket_addr_get_name(p->sa, &(out->port), out->name, CONN_NAME_BUFFER_SIZE);
}

static void
handle_recv(
	void *args
) {
	struct on_recv_args_ *a = (struct on_recv_args_*)args;
	if (!atomic_load_explicit(a->is_stop, memory_order_acquire))
	{
		a->on_recv(&(a->p), &(a->c), a->buf, a->b_size, a->args);
	}
	free(a->buf);
	free(a);
}

static struct conn_peer_i_ *
find_peer(
	struct conn_peer_i_ *list,
	util_socket_addr_s sa,
	struct timespec *now
) {
	struct conn_peer_i_ *n = list;
	while (n != NULL)
	{
		if (util_socket_addr_is_equals(n->sa, sa))
		{
			// same peer
			cpy_timespec(&n->lastTs, now);
			return n;
		}
		n = n->next;
	}
	return NULL;
}

static struct conn_peer_i_ *
add_peer(
	struct conn_peer_i_ *list,
	util_socket_s s,
	util_socket_addr_s sa,
	conn_server_type_e t,
	struct timespec *now,
	long *toAdd
) {
	struct conn_peer_i_ *n = (struct conn_peer_i_ *)calloc(1, sizeof(struct conn_peer_i_));
	n->sa = util_socket_addr_new();
	util_socket_addr_cpy(n->sa, sa);
	n->socket = s;
	n->t = t;
	cpy_timespec(&n->lastTs, now);
	if (toAdd)
	{
		*toAdd += 1;
	}
	n->next = list;
	return n;
}

static struct conn_peer_i_ *
rm_peer(
	struct server_args_ *a,
	struct timespec *now,
	void *buf,
	int *got_msg,
	double elapsedTime,
	int remAll
) {
	struct conn_peer_i_ *list = a->peers;
	struct conn_peer_i_ *ant = list;
	struct conn_peer_i_ *n = list;
	struct conn_peer_i_ *ret = list;
	int recv_len;
	struct conn_peer_ peer;
	struct conn_ conn;

	util_socket_s socket = a->socket;
	long *listSize = &a->nbPeers;
	_Atomic int *is_stop = a->isStop;
	void (*on_waiting)(conn_peer_s, conn_s, double, void *);
	void (*on_recv)(conn_peer_s,conn_s,void*,long,void*) = a->bindings.on_recv;
	void (*on_error)(conn_peer_s, conn_error_type_e, void *) = a->bindings.on_error;
	void *args = a->args;

	if (!remAll)
		on_waiting = a->bindings.on_waiting;
	else
		on_waiting = a->bindings.on_stop;

	while (n != NULL)
	{
		int isRem = 0;
		isRem = diff_timespec_ms(&n->lastTs, now) > TIMEOUT_MS;
		if (remAll || isRem)
		{
			*listSize -= 1;
			if (n == list)
				ret = n->next;
			else
			{
				ant = n;
			}
		}
		if (remAll || !isRem)
		{
			conn.sa = n->sa;
			conn.socket = n->socket;
			memset(&peer, 0, sizeof(peer));
			i_peer_decode(n, &peer);
			on_waiting(&peer, &conn, elapsedTime, args);
		}
		if (CONN_TCP == n->t && !atomic_load_explicit(is_stop, memory_order_acquire))
		{
			int no_msg = util_socket_recv(n->socket, n->sa, buf, CONN_BUFFER_SIZE, &recv_len);
			if (recv_len >= CONN_BUFFER_SIZE)
				fprintf(stderr, "Message size (%iB) possibly larger than internal buffer (%iB)\n", recv_len, CONN_BUFFER_SIZE);

			if (!no_msg && recv_len > 0)
			{
				if (0 == no_msg)
				{
					cpy_timespec(&n->lastTs, now);
					if (got_msg)
						*got_msg = 1;
					struct on_recv_args_ *a = (struct on_recv_args_*)malloc(sizeof(struct on_recv_args_));
					a->is_stop = is_stop;
					a->c.sa = n->sa;
					a->c.socket = n->socket;
					memset(&a->p, 0, sizeof(struct conn_peer_));
					i_peer_decode(n, &a->p);
					a->buf = malloc(((recv_len+63L)&-64L));
					memcpy(a->buf, buf, recv_len);
					a->b_size = recv_len;
					a->on_recv = on_recv;
					a->args = args;

					struct util_thrd_work_ w;
					w.callback = handle_recv;
					w.args = a;
					util_thrd_push_work(thrds, &w);
				}
				else
				{
					i_peer_decode(n, &peer);
					on_error(&peer, no_msg, args);
				}
			}
		}
		if (remAll || isRem)
		{
			struct conn_peer_i_ *rem = n;
			while (ret == rem)
			{
				ret = ret->next;
				if (list != rem)
				{
					ant->next = n->next;
				}
			}
			ant = n;
			n = n->next;
			if (CONN_TCP == rem->t && a->is_server)
			{
				util_socket_shutdown(rem->socket);
				util_socket_destroy(rem->socket);
			}
			util_socket_addr_destroy(rem->sa);
			free(rem);
		}
		else
		{
			ant = n;
			n = n->next;
		}
	}
	return ret;
}

static int
server_UDP(
	void *args
) {
	struct server_args_ *a = (struct server_args_ *)args;
	int recv_len;
	void *buf = malloc(CONN_BUFFER_SIZE);
	double elapsed = 0.0;
	long backoff = 1024;
	struct timespec ts, now, backup_now;
	util_socket_addr_s sa = a->sa;
	int no_msg = 0;
	util_clock_set_type(UTIL_CLOCK_FASTER);

	a->nbPeers = 0;
	a->peers = NULL;

	a->bindings.on_ready(a->args);
	atomic_store_explicit(a->isReady, 1, memory_order_release);

	util_clock_gettime(&ts);
	while (!atomic_load_explicit(a->isStop, memory_order_acquire))
	{
		util_clock_gettime(&now);
		a->peers = rm_peer(
			a,
			&now,
			buf,
			NULL,
			diff_timespec_ms(&ts, &now),
			/* remAll */ 0
		); // check timeouts
		cpy_timespec(&ts, &now);
		cpy_timespec(&backup_now, &now);
		now.tv_sec = 0;
		now.tv_nsec = backoff;

		int no_msg = util_socket_recv(a->socket, sa, buf, CONN_BUFFER_SIZE, &recv_len);
		if (1 == no_msg)
		{
			if (backoff < CONN_MAX_BACKOFF_NS)
			{
				backoff <<= 1;
			}
			thrd_sleep(&now, &ts);
			continue;
		}
		else
			backoff = 1024; // reset backoff

		if (recv_len >= CONN_BUFFER_SIZE)
			fprintf(stderr, "Message size (%iB) possibly larger than internal buffer (%iB)\n", recv_len, CONN_BUFFER_SIZE);

		struct conn_peer_i_ *p = find_peer(a->peers, sa, &backup_now);
		if (p == NULL)
		{
			a->peers = add_peer(
				a->peers,
				a->socket,
				sa,
				CONN_UDP,
				&backup_now,
				&a->nbPeers);
			p = a->peers;
		}
		if (!(no_msg < 0))
		{
			// a->bindings.on_recv(&peer, &conn, buf, recv_len, a->args);
			struct on_recv_args_ *recv_a = (struct on_recv_args_*)malloc(sizeof(struct on_recv_args_));
			recv_a->is_stop = a->isStop;
			recv_a->c.sa = sa;
			recv_a->c.socket = a->socket;
			util_socket_connect_sa(a->socket, sa);
			memset(&recv_a->p, 0, sizeof(struct conn_peer_));
			i_peer_decode(p, &recv_a->p);
			recv_a->buf = malloc(((recv_len+63L)&-64L));
			memcpy(recv_a->buf, buf, recv_len);
			recv_a->b_size = recv_len;
			recv_a->on_recv = a->bindings.on_recv;
			recv_a->args = args;

			struct util_thrd_work_ w;
			w.callback = handle_recv;
			w.args = recv_a;
			util_thrd_push_work(thrds, &w);
		}
		else
		{
			struct conn_peer_ peer;
			i_peer_decode(p, &peer);
			a->bindings.on_error(&peer, no_msg, a->args);
		}
	}
exit:
	util_clock_gettime(&now);
	a->peers = rm_peer(
		a,
		&now,
		buf,
		NULL,
		diff_timespec_ms(&ts, &now),
		/*remAll*/ 1
	); // check timeouts
	backup_now.tv_sec = 0;
	backup_now.tv_nsec = 1<<24;
	thrd_sleep(&backup_now, &now); // TODO: wait background recvs
	free(args);
	free(buf);
	util_socket_addr_destroy(sa);
	return 0;
}

static int
server_TCP(
	void *args
) {
	struct server_args_ *a = (struct server_args_ *)args;
	int recv_len;
	void *buf = malloc(CONN_BUFFER_SIZE);
	double elapsed = 0.0;
	long backoff = 1024;
	struct timespec ts, now, rem;
	util_socket_addr_s sa;
	int no_msg = 0;
	int got_msg = 0;
	util_clock_set_type(UTIL_CLOCK_FASTER);

	a->nbPeers = 0;
	a->peers = NULL;

	util_clock_gettime(&ts);
	cpy_timespec(&now, &ts);

	sa = a->sa;
	if (a->is_server)
	{
		util_socket_listen(a->socket);
	}
	else
	{
		a->peers = add_peer(
			a->peers,
			a->socket,
			sa,
			CONN_TCP,
			&ts,
			&a->nbPeers);
	}
	
	a->bindings.on_ready(a->args);
	atomic_store_explicit(a->isReady, 1, memory_order_release);

	while (!atomic_load_explicit(a->isStop, memory_order_acquire))
	{
		util_socket_s s = NULL;
		util_clock_gettime(&now);
		if (a->is_server)
		{
			s = util_socket_accept(a->socket, sa);
			if (NULL != s)
			{
				if (NULL == find_peer(a->peers, sa, &now))
				{
					a->peers = add_peer(
						a->peers,
						s,
						sa,
						CONN_TCP,
						&now,
						&a->nbPeers);
				}
			}
		}
		else
		{
			s = a->socket;
		}

		// goes over all the connections
		a->peers = rm_peer(
			a,
			&now,
			buf,
			&got_msg,
			diff_timespec_ms(&ts, &now),
			/* remAll */ 0
		); // check timeouts
		cpy_timespec(&ts, &now);
		
		now.tv_sec = 0;
		now.tv_nsec = backoff;
		if (!got_msg)
		{
			thrd_sleep(&now, &rem);
			if (backoff < CONN_MAX_BACKOFF_NS)
			{
				backoff <<= 1;
			}
		}
		else
		{
			backoff = 1024; // reset backoff
		}
	}

exit:
	util_clock_gettime(&now);
	a->peers = rm_peer(
		a,
		&now,
		buf,
		&got_msg,
		diff_timespec_ms(&ts, &now),
		/*remAll*/ 1
	); // check timeouts
	rem.tv_sec = 0;
	rem.tv_nsec = 1<<24;
	thrd_sleep(&rem, &now); // TODO: wait background recvs
	free(args);
	free(buf);
	util_socket_addr_destroy(sa);
	return 0;
}

static util_socket_s
new_socket(
	conn_server_type_e t
) {
	util_socket_set_non_blocking(1);
	if (t == CONN_UDP)
	{
		return util_socket_new(UTIL_SOCKET_PROT_DATAGRAM);
	}
	if (t == CONN_TCP)
	{
		return util_socket_new(UTIL_SOCKET_PROT_STREAM);
	}
	return NULL;
}

void
conn_init(
	int nbOnRecvThrs
) {
	thrds = util_thrd_start_pool(nbOnRecvThrs);
}

void
conn_exit()
{
	util_thrd_stop_pool(thrds);
}

conn_server_s
conn_server_start(
	conn_server_bindings_s c,
	long p,
	conn_server_type_e t,
	void *args
) {
	conn_server_s ret;
	struct server_args_ *server_args;
	util_socket_s socket;

	ret = (conn_server_s)malloc(sizeof(struct conn_server_));
	assert(ret != NULL && "malloc failed");

	if (!(socket = new_socket(t)))
	{
		free(ret);
		return NULL;
	}
	ret->s_type = t;
	ret->socket = socket;
	ret->isStop = 0;
	ret->isReady = 0;

	if (util_socket_bind(socket, NULL, p))
	{
		util_socket_destroy(socket);
		free(ret);
		return NULL;
	}

	server_args = (struct server_args_ *)malloc(sizeof(struct server_args_));
	server_args->sa = util_socket_addr_new();
	server_args->socket = socket;
	server_args->is_server = 1;
	server_args->bindings = *c;
	server_args->isStop = &ret->isStop;
	server_args->isReady = &ret->isReady;
	server_args->args = args;

	int (*server)(void *);
	if (t == CONN_UDP)
		server = server_UDP;
	if (t == CONN_TCP)
		server = server_TCP;
	if (thrd_create(&ret->thread, server, (void *)server_args))
	{
		util_socket_destroy(socket);
		free(ret);
		REPORT_ERROR;
		free(server_args);
		return NULL;
	}
	while(!atomic_load_explicit(server_args->isReady, memory_order_acquire));
	return ret;
}

int conn_server_get_port(conn_server_s s)
{
	return util_socket_get_port(s->socket);
}

int
conn_server_stop(
	conn_server_s c
) {
	atomic_store_explicit(&c->isStop, 1, memory_order_release);
	util_socket_shutdown(c->socket);
	thrd_join(c->thread, NULL);
	util_socket_destroy(c->socket);
	free(c);
	return 0;
}

int
conn_send(
	conn_s c,
	void *buffer,
	long size
) {
	return util_socket_send(c->socket, buffer, size);
}

conn_s
conn_connect(
	const char *hostName,
	long p,
	conn_server_type_e t,
	conn_bindings_s recv,
	void *args
) {
	struct server_args_ *server_args;
	util_socket_s socket = new_socket(t);
	conn_s ret;
	util_socket_addr_s sa = util_socket_addr_new();
	ret = (struct conn_ *)malloc(sizeof(struct conn_));
	ret->socket = socket;
	ret->isStop = 0;
	ret->isReady = 0;
	server_args = (struct server_args_ *)malloc(sizeof(struct server_args_));
	server_args->socket = socket;
	server_args->is_server = 0;
	server_args->isStop = &ret->isStop;
	server_args->isReady = &ret->isReady;
	server_args->bindings.on_recv = recv->on_recv;
	server_args->bindings.on_ready = recv->on_ready;
	server_args->bindings.on_stop = recv->on_disconnect;
	server_args->bindings.on_waiting = recv->on_waiting;
	server_args->bindings.on_error = recv->on_error;
	server_args->args = args;
	server_args->sa = sa;
	util_socket_connect(socket, hostName, p, sa);

	int (*server)(void *);
	if (t == CONN_UDP)
		server = server_UDP;
	if (t == CONN_TCP)
		server = server_TCP;
	if (thrd_create(&ret->thread, server, (void *)server_args))
	{
	error:
		util_socket_destroy(socket);
		free(ret);
		REPORT_ERROR;
		free(server_args);
		return NULL;
	}
	// wait server
	while(!atomic_load_explicit(server_args->isReady, memory_order_acquire));
	return ret;
}

int conn_disconnect(conn_s c)
{
	atomic_store_explicit(&c->isStop, 1, memory_order_release);
	util_socket_shutdown(c->socket);
	thrd_join(c->thread, NULL);
	util_socket_destroy(c->socket);
	free(c);
	return 0;
}
