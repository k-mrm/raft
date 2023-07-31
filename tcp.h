#ifndef RAFT_TCP_H
#define RAFT_TCP_H

struct tcpchnl {
	int fd;
};

struct tcpchnl *tcp_accept(int listenfd);
struct tcpchnl *connect_tcp(const char *host, const char *port);
void tcp_disconnected(struct tcpchnl *t);
int tcp_listen(int port);
ssize_t tcp_write(struct tcpchnl *t, unsigned char *buf, size_t n);
ssize_t tcp_read(struct tcpchnl *t, unsigned char *buf, size_t n);

#endif
