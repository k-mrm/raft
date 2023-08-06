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
	terminated = 1;
}

static int
clientmain() {
	char msg[12];
	size_t s;

	while (!terminated) {
		printf("> ");
		scanf("%12s", msg);
		s = tcpSend(serv, msg, sizeof msg);
		if (s == 0)
			break;
		memset(msg, 0, sizeof msg);
	}

	tcpDisconnect(serv);
	return 0;
}

static int
doClient(char *servIp) {
	int rc;

	serv = tcpConnect(servIp, 1145);
	if (!serv)
		return -1;

	for (;;) {
		rc = clientmain();
		if (rc)
			return rc;
	}
}

int
main(int argc, char **argv) {
	if (argc < 2)
		return -1;

	signal(SIGINT, sighandler);
	signal(SIGTERM, sighandler);

	return doClient(argv[1]);
}
