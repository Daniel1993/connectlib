#include "connectlib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

volatile int server_is_ready = 0;

static void on_ready()
{
	printf("Ready!\n");
	server_is_ready = 1;
}

static void on_stop(conn_peer_s p, conn_s c, double time_elapsed_ms, void *args)
{
	printf("peer %s:%i: Stop (after %2.3fms, %2.3fms without answer)!\n", p->name, p->port, time_elapsed_ms, p->time_since_ms);
}

static void on_waiting(conn_peer_s p, conn_s c, double time_elapsed_ms, void *args)
{
	// printf("peer %s:%i: Waited %2.3fms without answer (%2.3fms since last call)!\n", p->name, p->port, p->time_since_ms, time_elapsed_ms);
}

static void on_recv(conn_peer_s p, conn_s c, void *buffer, long size, void *args)
{
	double *hiden = (double*)buffer;
	printf("Recv \"%s\" from %s:%i lat %2.2fms (hidden = %lf)!\n", buffer, p->name, p->port, p->time_since_ms, hiden[12]);
	sprintf((char*)buffer, "ack");
	conn_send(c, buffer, size);
}

static void on_recv2(conn_peer_s p, conn_s c, void *buffer, long size, void *args)
{
	double *hiden = (double*)buffer;
	printf(">Recv2< \"%s\" from %s:%i lat %2.2fms (hidden = %lf)!\n", buffer, p->name, p->port, p->time_since_ms, hiden[12]);
	sprintf((char*)buffer, "ack2");
	conn_send(c, buffer, size);
}

static void on_error(conn_peer_s p, conn_error_type_e err, void *args)
{
	printf("ERROR %i!\n", err);
}

#define SIZE_MSG 128

int main()
{
	conn_init(/* nb thrds */0);
	struct conn_server_bindings_ b;
	struct conn_bindings_ recv;
	char msg[SIZE_MSG];
	b.on_ready = on_ready;
	b.on_stop = on_stop;
	b.on_waiting = on_waiting;
	b.on_recv = on_recv;
	b.on_error = on_error;
	conn_server_s server = conn_server_start(&b, -1, CONN_UDP, NULL);
	recv.on_ready = on_ready;
	recv.on_disconnect = on_stop;
	recv.on_recv = on_recv2;
	recv.on_error = on_error;
	recv.on_waiting = on_waiting;
	memset(msg, 0, SIZE_MSG);
	((double*)msg)[12] = -1234556789.123456; // hidden
	while (!server_is_ready); // wait server ready before connecting!
	conn_s c1 = conn_connect("localhost", conn_server_get_port(server), CONN_UDP, &recv, NULL);
	conn_s c2 = conn_connect("localhost", conn_server_get_port(server), CONN_UDP, &recv, NULL);
	conn_s c3 = conn_connect("localhost", conn_server_get_port(server), CONN_UDP, &recv, NULL);

	sprintf(msg, " (1) Hello world!");
	conn_send(c1, msg, SIZE_MSG);
	sprintf(msg, "Good bye!");
	conn_send(c2, msg, SIZE_MSG);

	sleep(1);
	sprintf(msg, "Meh!");
	conn_send(c3, msg, SIZE_MSG);

	// TODO: disconecting while messages are still around may cause issues
	sleep(1);

	conn_disconnect(c1);
	conn_disconnect(c2);
	conn_disconnect(c3);
	conn_server_stop(server);

	conn_exit();
	return 0;
}
