#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include "tcp.h"

TCP *serv;

static int
clientmain() {
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

	return doClient(argv[1]);
}
