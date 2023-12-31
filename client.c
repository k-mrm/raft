#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <signal.h>
#include "tcp.h"
#include "cmd.h"

TCP *serv;
int terminated = 0;

static void
sighandler(int sig) {
	(void)sig;
	terminated = 1;
}

static int
clientmain() {
	char msg[12];
	size_t s;
	int r;

	printf("internal cmd p: print log, c: print committed log\n");
	while (!terminated) {
		printf("> ");
		r = scanf("%12s", msg);
		if (r == EOF) {
			break;
		}
		s = tcpSend(serv, msg, sizeof msg);
		if (s == 0) {
			break;
		}
		memset(msg, 0, sizeof msg);
	}

	tcpDisconnect(serv);
	return 0;
}

static int
doClient(char *servIp) {
	serv = tcpConnect(servIp, 1145, 1919);
	if (!serv)
		return -1;

	return clientmain();
}

int
main(int argc, char **argv) {
	if (argc < 2)
		return -1;

	signal(SIGINT, sighandler);

	return doClient(argv[1]);
}
