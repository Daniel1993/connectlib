#include "connectlib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#define SIZE_MSG 128
static char msg[SIZE_MSG];
static conn_s ping;

static void on_ready()
{
	printf("Ready!\n");
}

static void on_stop(conn_peer_s p, conn_s c, double time_elapsed_ms, void *args)
{
	printf("peer %s:%i: Stop (after %2.3fms, %2.3fms without answer)!\n", p->name, p->port, time_elapsed_ms, p->time_since_ms);
}

static void on_waiting(conn_peer_s p, conn_s c, double time_elapsed_ms, void *args)
{
	printf("peer %s:%i: Waited %2.3fms without answer (%2.3fms since last call)!\n", p->name, p->port, p->time_since_ms, time_elapsed_ms);
}

static void on_recv(conn_peer_s p, conn_s c, void *buffer, long size, void *args)
{
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
	conn_disconnect(ping);
	conn_exit();
	exit(0);
}

int
main(
	int argc, 
	char **argv
) {
	conn_server_type_e t = CONN_TCP;
	struct conn_bindings_ recv;
	struct sigaction new_action;

	if (argc < 4)
	{
		printf("Usage %s <IP> <PORT> <TCP|UDP>\n", argv[0]);
		return 0;
	}

	conn_init(/* nb thrds */0);
	
	recv.on_ready = on_ready;
	recv.on_disconnect = on_stop;
	recv.on_waiting = on_waiting;
	recv.on_recv = on_recv;
	recv.on_error = on_error;

	if (strcmp("UDP", argv[3]) == 0)
		t = CONN_UDP;
	
	ping = conn_connect(argv[1], atoi(argv[2]), t, &recv, NULL);
	
	sigemptyset(&new_action.sa_mask);
	new_action.sa_sigaction = termination_handler;

	sigaction(SIGINT, &new_action, NULL);
	sigaction(SIGHUP, &new_action, NULL);
	sigaction(SIGTERM, &new_action, NULL);
	
	while(1)
	{
		sprintf(msg, "ping");
		conn_send(ping, msg, strlen(msg)+1);
		sleep(1); // terminate with ctrl+c
	}

	conn_exit();
	return 0;
}
