#ifndef RAFT_TCP_H
#define RAFT_TCP_H

typedef struct TCP TCP;
struct TCP {
	int fd;
};

TCP *tcp_accept(int listenfd);
TCP *connect_tcp(const char *host, int port);
void tcp_disconnected(TCP *t);
int tcp_listen(int port);
ssize_t tcp_write(TCP *t, unsigned char *buf, size_t n);
ssize_t tcp_read(TCP *t, unsigned char *buf, size_t n);

#endif
