#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <signal.h>
#include <sys/timerfd.h>
#include <assert.h>
#include <poll.h>
#include <arpa/inet.h>
#include "tcp.h"

char *iplist[3] = {
	"10.0.0.100",
	"10.0.0.1",
	"10.0.0.2",
};

typedef enum RSTATE RSTATE;
enum RSTATE {
	NONESTATE,
	FOLLOWER,
	CANDIDATE,
	LEADER,
};

typedef struct RAFTPEER RAFTPEER;
struct RAFTPEER {
	TCP *wrch;
	TCP *rdch;

	struct sockaddr_in addr;
	int peerid;

	bool active;
};

#define foreachPeer(s, p)		\
		int _np = (s)->npeers;	\
		for (p = (s)->peers; (p) < &(s)->peers[8] && _np; (p)++)	\
			if ((p)->active && _np--)

typedef struct RAFTSERVER RAFTSERVER;
struct RAFTSERVER {
	RSTATE state;

	int htimeout_lo;
	int htimeout_hi;

	int socket;
	char *ipaddr;

	RAFTPEER peers[8];
	int npeers;

	int myid;	

	timer_t timer;
	int heartbeat_tick;

	int curterm;

	int votes;
	bool voted;	// already voted?

	RAFTPEER *leader;
};

typedef enum RPCTYPE RPCTYPE;
enum RPCTYPE {
	NONRPCTYPE,
	REQUEST_VOTE,
	REQUEST_VOTE_REPLY,
	APPEND_ENTRIES,
	APPEND_ENTRIES_REPLY,
};

typedef struct RPC RPC;
struct RPC {
	RPCTYPE type;
	int term;
};

typedef struct REQUEST_VOTE_RPC REQUEST_VOTE_RPC;
struct REQUEST_VOTE_RPC {
	RPC rpc;
	int candidateId;
	int lastLogindex;
	int lastLogterm;
};

typedef struct REQUEST_VOTE_REP_RPC REQUEST_VOTE_REP_RPC;
struct REQUEST_VOTE_REP_RPC {
	RPC rpc;
	bool voteGranted;
};

typedef struct APPEND_ENTRIES_RPC APPEND_ENTRIES_RPC;
struct APPEND_ENTRIES_RPC {
	RPC rpc;
	int leaderId;
	int prevLogindex;
	int prevLogterm;
	char entries[32];
	int leaderCommit;
};

typedef struct APPEND_ENTRIES_REP_RPC APPEND_ENTRIES_REP_RPC;
struct APPEND_ENTRIES_REP_RPC {
	RPC rpc;
	bool success;
};

static void
bzero(void *buf, size_t s) {
	memset(buf, 0, s);
}

static RAFTPEER *
peerbyid(RAFTSERVER *s, int peerid) {
	RAFTPEER *p;

	foreachPeer(s, p) {
		if (p->peerid == peerid)
			return p;
	}

	return NULL;
}

struct RAFTPEER *
peerbyip(RAFTSERVER *s, struct sockaddr_in addr) {
	RAFTPEER *p;

	foreachPeer(s, p) {
		if (p->addr.sin_addr.s_addr == addr.sin_addr.s_addr)
			return p;
	}

	return NULL;
}

static void
sendrpc(RAFTSERVER *s, RPC *rpc, size_t size, RAFTPEER *target) {
	rpc->term = s->curterm;

	tcp_write(target->wrch, rpc, size);
}

static size_t
readrpc(RAFTPEER *from, RPC *rpc, size_t bufsize) {
	TCP *rd = from->rdch;

	return tcp_read(rd, rpc, bufsize);
}

static void
peerDisconnected(RAFTSERVER *s, RAFTPEER *peer) {
	tcp_disconnected(peer->wrch);
	tcp_disconnected(peer->rdch);
	peer->wrch = NULL;
	peer->rdch = NULL;
	peer->active = false;

	s->npeers--;
}

static bool
validRequest(REQUEST_VOTE_RPC *rpc) {
	// TODO
	return true;
}

static void
recvRequestVote(RAFTSERVER *s, RAFTPEER *from, REQUEST_VOTE_RPC *rpc) {
	REQUEST_VOTE_REP_RPC reply;
	bool vote = false;

	printf("recv requestvote!\n");

	if (s->state == LEADER)
		printf("????leader\n");

	reply.rpc.type = REQUEST_VOTE_REPLY;

	// Is a candidate new?
	if (!s->voted && ((RPC *)rpc)->term > s->curterm) {
		s->voted = true;
		vote = true;	
	}

	reply.voteGranted = vote;

	sendrpc(s, (RPC *)&reply, sizeof reply, from);
}

static bool
isMajority(RAFTSERVER *s) {
	return s->votes * 2 > s->npeers + 1;
}

static void
voteDone(RAFTSERVER *s) {
	s->votes = 0;
	s->voted = false;
}

static void
recvRequestVoteRep(RAFTSERVER *s, RAFTPEER *from, REQUEST_VOTE_REP_RPC *rpc) {
	printf("recv requestvote reply!\n");

	if (rpc->voteGranted) {
		printf("voted from %d\n", from->peerid);
		s->votes++;
	}

	if (isMajority(s)) {
		printf("won vote! become a leader\n");
		s->state = LEADER;
		voteDone(s);
	}
}

static bool
biszero(void *buf, size_t size) {
	char zero[size];

	memset(zero, 0, size);
	return !memcmp(buf, zero, size);
}   

static void
recvHeartbeat(RAFTSERVER *s, RAFTPEER *from, APPEND_ENTRIES_RPC *rpc) {
	// TODO
	if (s->state == LEADER)
		printf("leader recv heartbeat\n");

	// old heartbeat is denied
	if (((RPC *)rpc)->term < s->curterm)
		return;

	if (s->state == CANDIDATE) {
		// other peer became LEADER, election is end
		printf("Term%d: now leader is %d!!!!!!!!!!\n", s->curterm, from->peerid);
		s->state = FOLLOWER;
		voteDone(s);
	}

	s->leader = from;
}

static void
recvAppendEntries(RAFTSERVER *s, RAFTPEER *from, APPEND_ENTRIES_RPC *rpc) {
	// printf("recv append entries!\n");
	
	if (biszero(rpc->entries, sizeof rpc->entries)) {	// is heartbeat?
		recvHeartbeat(s, from, rpc);
		return;
	}

	printf("entries: \n");
}

static void
recvAppendEntriesRep(RAFTSERVER *s, RAFTPEER *from, APPEND_ENTRIES_REP_RPC *rpc) {
	printf("recv append entries reply!\n");
}

static void
recvrpc(RAFTSERVER *s, RAFTPEER *from) {
	char buf[512];
	RPC *rpc = (RPC *)buf;
	size_t rpcsize;

	rpcsize = readrpc(from, rpc, 512);
	if (rpcsize == 0)
		peerDisconnected(s, from);

	switch (rpc->type) {
	case REQUEST_VOTE:
		recvRequestVote(s, from, (REQUEST_VOTE_RPC *)rpc);
		break;
	case REQUEST_VOTE_REPLY:
		recvRequestVoteRep(s, from, (REQUEST_VOTE_REP_RPC *)rpc);
		break;
	case APPEND_ENTRIES:
		recvAppendEntries(s, from, (APPEND_ENTRIES_RPC *)rpc);
		break;
	case APPEND_ENTRIES_REPLY:
		recvAppendEntriesRep(s, from, (APPEND_ENTRIES_REP_RPC *)rpc);
		break;
	default:
		return;
	}
}

static void
rpcbcast(RAFTSERVER *s, RPC *rpc, size_t size) {
	RAFTPEER *peer;

	foreachPeer(s, peer) {
		sendrpc(s, rpc, size, peer);
	}
}

static void
requestVote(RAFTSERVER *s) {
	REQUEST_VOTE_RPC rpc;

	assert(s->state == CANDIDATE);
	printf("request vote\n");

	rpc.rpc.type = REQUEST_VOTE;
	rpc.candidateId = s->myid;
	rpc.lastLogindex = 0;
	rpc.lastLogterm = 0;

	rpcbcast(s, (RPC *)&rpc, sizeof rpc);
}

static void
sendHeartbeat(RAFTSERVER *s) {
	APPEND_ENTRIES_RPC rpc;

	if (s->state != LEADER)
		return;

	rpc.rpc.type = APPEND_ENTRIES;
	rpc.leaderId = s->myid;
	rpc.prevLogindex = 0;
	rpc.prevLogterm = 0;
	bzero(rpc.entries, sizeof rpc.entries);
	rpc.leaderCommit = 0;	// TODO

	rpcbcast(s, (RPC *)&rpc, sizeof rpc);
}

void
heartbeat(union sigval sv) {
	RAFTSERVER *s = sv.sival_ptr;

	sendHeartbeat(s);
}

static struct timespec
ms2timespec(int ms) {
	struct timespec ts;
	ts.tv_sec = ms / 1000;	
	ts.tv_nsec = (ms % 1000) * 1000000;
	return ts;
}

static int
heartbeatTimeout(RAFTSERVER *s) {
	int high, low, range;

	high = s->htimeout_hi;
	low = s->htimeout_lo;
	range = high - low;
	if (range < 0)
		return 0;

	srand(time(NULL));
	return low + rand() % range;
}

static void
tickinit(RAFTSERVER *s, void (*callback)(union sigval)) {
	struct sigevent se;
	struct itimerspec ts;
	timer_t timer;

	se.sigev_notify = SIGEV_THREAD;
	se.sigev_value.sival_ptr = (void *)s;
	se.sigev_notify_function = callback;
	se.sigev_notify_attributes = NULL;

	ts.it_value = ms2timespec(s->heartbeat_tick);
	ts.it_interval = ms2timespec(s->heartbeat_tick);

	if (timer_create(CLOCK_MONOTONIC, &se, &timer) < 0) {
		printf("timer_create!");
		return;
	}
	if (timer_settime(timer, 0, &ts, 0) < 0) {
		printf("timer_settime!");
		return;
	}

	s->timer = timer;
}

static void
connectallserv(RAFTSERVER *s, int nservs) {
	int peeridx = 0;
	int i = 0;
	RAFTPEER *peer;
	TCP *ch;
	int mask = (1 << nservs) - 1;
	int connected = 0;

	s->npeers = nservs - 1;

	do {
		if (connected & (1 << i))
			goto cnctd;

		usleep(100 * 1000);	// wait 100 ms

		if (inet_addr(iplist[i]) == inet_addr(s->ipaddr)) {
			connected |= 1 << i;
			goto cnctd;
		}

		peer = &s->peers[peeridx];
		ch = connect_tcp(iplist[i], 1145);
		if (ch) {
			connected |= 1 << i;
			peer->wrch = ch;
			peer->peerid = i;
			peer->addr = ch->addr;
			peer->active = true;
			peeridx++;
		}

	cnctd:
		i = (i + 1) % nservs;
	} while (connected != mask);
}

static void
serverinit(RAFTSERVER *s, int myid, int nservs) {
	int sock;
	char *myip = iplist[myid];
	int port = 1145;

	bzero(s, sizeof *s);
	s->state = NONESTATE;
	s->htimeout_lo = 150;
	s->htimeout_hi = 300;		// heartbeat timeout is 150-300 ms
	s->heartbeat_tick = 50;		// heartbeat per 50 ms
	s->npeers = 0;

	sock = tcp_listen(myip, port);	// establish tcp connection
	if (sock < 0) {
		printf ("listen failed\n");
		return;
	}
	printf("listen at %s:%d...\n", myip, port);
	s->socket = sock;
	s->myid = myid;
	s->ipaddr = myip;

	connectallserv(s, nservs);

	tickinit(s, heartbeat);

	s->state = FOLLOWER;

	printf("serverinitdone\n");
}

static void
election(RAFTSERVER *s) {
	// vote me
	s->votes++;
	s->voted = true;

	s->curterm++;

	requestVote(s);
}

static void
doHeartbeatTimeout(RAFTSERVER *s) {
	if (s->state == LEADER)
		return;

	printf("timeout, start election\n");

	if (s->state == CANDIDATE)	// re-election
		voteDone(s);

	s->state = CANDIDATE;
	election(s);
}

static int
raftpollfd(RAFTSERVER *s, struct pollfd *fds, RAFTPEER **peers) {
	int nfds;
	RAFTPEER *peer;
	TCP *rdch;
	int pi = 0;

	fds[0] = (struct pollfd){ .fd = s->socket, .events = POLLIN };
	nfds = 1;

	foreachPeer(s, peer) {
		rdch = peer->rdch;
		if (!rdch)
			continue;

		fds[nfds++] = (struct pollfd){ .fd = rdch->fd, .events = POLLIN };		
		peers[pi++] = peer;
	}

	return nfds;
}

static int
servermain(RAFTSERVER *s) {
	int nready;
	struct pollfd fds[16];
	RAFTPEER *peers[16];
	struct pollfd *pfd;
	RAFTPEER *peer;
	TCP *rdch;
	int nfds;
	int timeout = heartbeatTimeout(s);

	nfds = raftpollfd(s, fds, peers);

	nready = poll(fds, nfds, timeout);
	if (nready < 0)
		return -1;
	if (!nready) {
		doHeartbeatTimeout(s);
		return 0;
	}

	for (int i = 0; i < nfds && nready; i++) {
		pfd = &fds[i];
		if (!(pfd->revents & POLLIN))
			continue;
		
		if (pfd->fd == s->socket) {
			rdch = tcp_accept(s->socket);
			if (!rdch)
				return -1;

			peer = peerbyip(s, rdch->addr);
			if (!peer) {
				printf("no peer!\n");
				return -1;
			}
			peer->rdch = rdch;

			printf("new peer!: %d\n", peer->peerid);
		} else {
			peer = peers[i - 1];
			recvrpc(s, peer);
		}

		nready--;
	}

	return 0;
}

int
main(int argc, char *argv[]) {
	RAFTSERVER server;
	int rc, me;
	int n = 3;

	if (argc < 2)
		return -1;
	me = atoi(argv[1]);

	// ./raft 1 
	serverinit(&server, me, n);
	
	for (;;) {
		rc = servermain(&server);
		if (rc)
			return rc;
	}
}
