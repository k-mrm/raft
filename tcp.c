#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <poll.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <net/ethernet.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <linux/if_packet.h>
#include "tcp.h"

TCP *
tcpAccept(int listenfd) {
	TCP *t;
	int clientfd;
	struct sockaddr_in client_sin;
	socklen_t sin_len = sizeof(client_sin);
	char s[32];

	if((clientfd = accept(listenfd, (struct sockaddr *)&client_sin, &sin_len)) < 0) {
		perror("accept");
		return NULL;
	}
	if(sin_len <= 0) {
		perror("wtf");
		return NULL;
	}

	t = malloc(sizeof(*t));
	if(!t)
		return NULL;

	t->fd = clientfd;
	memcpy(&t->addr, &client_sin, sin_len);

	inet_ntop(AF_INET, &t->addr.sin_addr, s, 32);
	printf("accept @%s\n", s);

	return t;
}

static void
tcpFree(TCP *t) {
	close(t->fd);
	free(t);
}

TCP *
tcpConnect(const char *host, int port) {
	int sock;
	TCP *t;
	struct addrinfo hint = {0}, *res;
	char port_s[8];
	char s[32];

	sprintf(port_s, "%d", port);

	hint.ai_family = PF_INET;
	hint.ai_socktype = SOCK_STREAM;

	if(getaddrinfo(host, port_s, &hint, &res) < 0) {
		// perror("getaddrinfo");
		return NULL;
	}
	if((sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) < 0) {
		// perror("socket");
		return NULL;
	}
	if(connect(sock, res->ai_addr, res->ai_addrlen) < 0)
		goto err;

	t = malloc(sizeof(*t));
	if(!t)
		goto err;

	t->fd = sock;
	memcpy(&t->addr, res->ai_addr, res->ai_addrlen);

	inet_ntop(AF_INET, &t->addr.sin_addr, s, 32);
	printf("connection done: %s\n", s);

	return t;

err:
	close(sock);
	return NULL;
}

void
tcpDisconnect(TCP *t) {
	char s[32];

	inet_ntop(AF_INET, &t->addr.sin_addr, s, 32);
	printf("disconnect @%s\n", s);
	tcpFree(t);
}

// IPv4 only
int
tcpListen(char *ipaddr, int port) {
	int sock;
	int yes = 1;
	struct sockaddr_in sin;

	if((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket");
		return -1;
	}

	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	inet_pton(AF_INET, ipaddr, &sin.sin_addr);

	if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
		perror("setsockopt");
		goto err;
	}
	if(bind(sock, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
		perror("bind");
		goto err;
	}
	if(listen(sock, 5) < 0) {
		perror("listen");
		goto err;
	}

	return sock;

err:
	close(sock);
	return -1;
}

ssize_t
tcpSend(TCP *t, void *buf, size_t n) {
	return write(t->fd, buf, n);
}

ssize_t
tcpRecv(TCP *t, void *buf, size_t n) {
	return read(t->fd, buf, n);
}
