#ifndef RAFT_TCP_H
#define RAFT_TCP_H

#include <stdint.h>
#include <arpa/inet.h>

typedef struct TCP TCP;
struct TCP {
	int fd;
	struct sockaddr_in addr;	// IPv4 only
};

TCP *tcp_accept(int listenfd);
TCP *connect_tcp(const char *host, int port);
void tcp_disconnected(TCP *t);
int tcp_listen(char *ipaddr, int port);
ssize_t tcp_write(TCP *t, void *buf, size_t n);
ssize_t tcp_read(TCP *t, void *buf, size_t n);

#endif
