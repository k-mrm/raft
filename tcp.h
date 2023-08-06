#ifndef __RAFT_TCP_H
#define __RAFT_TCP_H

#include <stdint.h>
#include <arpa/inet.h>

typedef struct TCP TCP;
struct TCP {
	int fd;
	struct sockaddr_in addr;	// IPv4 only
};

TCP *tcpAccept(int listenfd);
TCP *tcpConnect(const char *host, int port, int myport);
void tcpDisconnect(TCP *t);
int tcpListen(char *ipaddr, int port);
ssize_t tcpSend(TCP *t, void *buf, size_t n);
ssize_t tcpRecv(TCP *t, void *buf, size_t n);

#endif	/* __RAFT_TCP_H */
