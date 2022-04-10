#include "connectlib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <assert.h>

#define SIZE_MSG 128
static conn_s *connections;
static int nb_connections;
static conn_server_s server;

typedef struct peer_data_
{
	double time_since;
}
peer_data_s;
static peer_data_s *peer_data;

static void on_ready() { /* empty */ }

static void on_stop(conn_peer_s p, conn_s c, double time_elapsed_ms, void *args) { /* empty */ }

static void on_waiting_server(conn_peer_s p, conn_s c, double time_elapsed_ms, void *args)
{ /* empty */ }

static void on_waiting_client(conn_peer_s p, conn_s c, double time_elapsed_ms, void *args)
{
	peer_data_s *a = (peer_data_s*)args;
	a->time_since += time_elapsed_ms;
	if (a->time_since > 1000.0) // wait 1 second
	{
		char msg[SIZE_MSG];
		a->time_since = 0;
		sprintf(msg, "ping");
		conn_send(c, msg, strlen(msg)+1);
	}
}

static void on_recv_server(conn_peer_s p, conn_s c, void *buffer, long size, void *args)
{
	char msg[SIZE_MSG];
	printf("Recv \"%s\" from %s:%i lat %2.2fms!\n", buffer, p->name, p->port, p->time_since_ms);
	sprintf(msg, "pong");
	conn_send(c, msg, strlen(msg)+1);
}

static void on_recv_client(conn_peer_s p, conn_s c, void *buffer, long size, void *args)
{
	peer_data_s *a = (peer_data_s*)args;
	a->time_since = 0;
	printf("Recv \"%s\" from %s:%i lat %2.2fms!\n", buffer, p->name, p->port, p->time_since_ms);
	// conn_send(c, msg, strlen(msg)+1);
}

static void on_error(conn_peer_s p, conn_error_type_e err, void *args)
{
	printf("ERROR %i!\n", err);
}

void
termination_handler(
	int signum,
	siginfo_t *si,
	void *unused
) {
	for (int i = 0; i < nb_connections; ++i)
	{
		conn_disconnect(connections[i]);
	}
	conn_server_stop(server);
	conn_exit();
	exit(0);
}

int
main(
	int argc, 
	char **argv
) {
	conn_server_type_e t = CONN_UDP;
	struct conn_bindings_ recv;
	struct conn_server_bindings_ b;
	struct sigaction new_action;
	int c = argc;

	if ((c-2) % 3 != 0)
	{
		printf("Usage %s <port> [--peer <IP> <PORT>]*\n  Given %i parameters\n", argv[0], argc);
		return 0;
	}

	conn_init(/* nb thrds */2);
	
	b.on_ready   = on_ready;
	b.on_stop    = on_stop;
	b.on_waiting = on_waiting_server;
	b.on_recv    = on_recv_server;
	b.on_error   = on_error;

	server = conn_server_start(&b, atoi(argv[1]), t, NULL);
	
	sigemptyset(&new_action.sa_mask);
	new_action.sa_sigaction = termination_handler;

	sigaction(SIGINT, &new_action, NULL);
	sigaction(SIGHUP, &new_action, NULL);
	sigaction(SIGTERM, &new_action, NULL);

	recv.on_ready = on_ready;
	recv.on_disconnect = on_stop;
	recv.on_waiting = on_waiting_client;
	recv.on_recv = on_recv_client;
	recv.on_error = on_error;

	if (c > 0)
	{
		int i = 2, j = (c-2) / 3, k = 0;
		connections = (conn_s*)malloc(sizeof(conn_s)*j);
		peer_data = (peer_data_s*)calloc(j, sizeof(peer_data_s));
		nb_connections = j;
		while (i < (c-2))
		{
			char msg[SIZE_MSG];
			assert(strcmp("--peer", argv[i]) == 0 && "missing --peer");
			connections[k] = conn_connect(argv[i+1], atoi(argv[i+2]), t, &recv, &peer_data[k]);
			sprintf(msg, "ping");
			conn_send(connections[k], msg, strlen(msg)+1);
			k++;
			i += 3;
		}
	}
	
	while(1)
		sleep(1); // terminate with ctrl+c

	conn_exit();
	return 0;
}
