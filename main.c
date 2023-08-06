#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <assert.h>
#include <poll.h>
#include <arpa/inet.h>
#include "tcp.h"
#include "cmd.h"

#define min(a, b)	((a) < (b) ? (a) : (b))

#define N	3

char *iplist[5] = {
	"10.0.0.100",
	"10.0.0.1",
	"10.0.0.2",
	"10.0.0.3",
	"10.0.0.4",
};

typedef enum RSTATE RSTATE;
enum RSTATE {
	DEAD,
	FOLLOWER,
	CANDIDATE,
	LEADER,
	NSTATE,
};

static const char *st[NSTATE] = {
	[DEAD] 		"DEAD",
	[FOLLOWER]	"FOLLOWER",
	[CANDIDATE]	"CANDIDATE",
	[LEADER]	"LEADER",
};

typedef struct RAFTPEER RAFTPEER;
struct RAFTPEER {
	TCP *wrch;
	TCP *rdch;

	struct sockaddr_in addr;
	int peerid;

	int nextIndex;
	int matchIndex;

	bool active;
};

#define foreachPeer(s, p)		\
	int _np = (s)->npeers;		\
	for (p = (s)->peers; (p) < &(s)->peers[8] && _np; (p)++)	\
		if ((p)->active && _np--)

// Raft client
typedef struct CLIENT CLIENT;
struct CLIENT {
	TCP *ch;
};

typedef struct MACHINESTATE MACHINESTATE;
struct MACHINESTATE {
	int var[26];
};

typedef struct LOG LOG;
struct LOG {
	int term;
	// COMMAND cmd;
	char s[12];	// tmp
};

typedef struct RAFTSERVER RAFTSERVER;
struct RAFTSERVER {
	RSTATE state;

	MACHINESTATE mstate;

	int htimeoutLo;
	int htimeoutHi;

	int socket;	// listen socket
	char *ipaddr;	// my ip address

	RAFTPEER peers[8];
	int npeers;

	RAFTPEER *leader;

	int myid;

	timer_t timer;
	int heartbeatTick;

	int curterm;	// currentTerm

	int votes;	// got votes
	int votefor;	// candidate id

	// log
	LOG log[256];
	int logIndex;	// last log index
	int commitIndex;
	int lastApplied;

	CLIENT *client;
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
	int lastLogIndex;
	int lastLogTerm;
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
	int prevLogIndex;
	int prevLogTerm;
	LOG entries[32];
	int leaderCommit;
};

typedef struct APPEND_ENTRIES_REP_RPC APPEND_ENTRIES_REP_RPC;
struct APPEND_ENTRIES_REP_RPC {
	RPC rpc;
	bool success;
};

static void __appendEntries(RAFTSERVER *s, RAFTPEER *p, bool heartbeat);

#define rinfo(s, fmt, ...)	\
	printf("R%d[%s] (Term%d): " fmt, (s)->myid, st[(s)->state], (s)->curterm, ##__VA_ARGS__)

static void
bzero(void *buf, size_t s) {
	memset(buf, 0, s);
}

static void
setCurTerm(RAFTSERVER *s, int curterm) {
	rinfo(s, "update Term: -> %d\n", curterm);
	s->curterm = curterm;
}

static void
becomeLeader(RAFTSERVER *s) {
	RAFTPEER *p;

	s->state = LEADER;
	s->leader = NULL;

	foreachPeer (s, p) {
		p->nextIndex = s->logIndex + 1;
		p->matchIndex = -1;
	}
}

static void
voteDone(RAFTSERVER *s) {
	s->votes = 0;
	s->votefor = -1;
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

	tcpSend(target->wrch, rpc, size);
}

static size_t
readrpc(RAFTPEER *from, RPC *rpc, size_t bufsize) {
	TCP *rd = from->rdch;

	return tcpRecv(rd, rpc, bufsize);
}

static void
peerDisconnect(RAFTSERVER *s, RAFTPEER *peer) {
	tcpDisconnect(peer->wrch);
	tcpDisconnect(peer->rdch);
	peer->wrch = NULL;
	peer->rdch = NULL;
	peer->active = false;

	s->npeers--;
}

static bool
peerIsMoreUptoDate(RAFTSERVER *s, int lastindex, int lastterm) {
	if (s->logIndex < 0) {
		return true;
	}

	if (lastterm == s->log[s->logIndex].term) {
		return lastindex >= s->logIndex;
	} else {
		return lastterm > s->log[s->logIndex].term;
	}
}

static void
recvRequestVote(RAFTSERVER *s, RAFTPEER *from, REQUEST_VOTE_RPC *rpc) {
	REQUEST_VOTE_REP_RPC reply;
	bool vote = false;
	bool validvote = false;

	reply.rpc.type = REQUEST_VOTE_REPLY;
	rinfo(s, "recv requestvote from: %d(T%d)\n", from->peerid, ((RPC *)rpc)->term);

	if (((RPC *)rpc)->term == s->curterm) {
		if (peerIsMoreUptoDate(s, rpc->lastLogIndex, rpc->lastLogTerm)) {
			validvote = true;
		}
	}

	if (s->votefor < 0 && validvote) {
		s->votefor = from->peerid;
		vote = true;
		rinfo(s, "voted for %d\n", s->votefor);
	} else {
		rinfo(s, "cannot vote for %d\n", from->peerid);
	}

	reply.voteGranted = vote;
	sendrpc(s, (RPC *)&reply, sizeof reply, from);
}

static bool
isMajority(RAFTSERVER *s) {
	return s->votes * 2 > s->npeers + 1;
}

static void
voteme(RAFTSERVER *s) {
	s->votes++;

	if (isMajority(s)) {
		rinfo(s, "won vote! become a leader: ./client %s\n", s->ipaddr);
		becomeLeader(s);
		voteDone(s);
	}
}

static void
recvRequestVoteRep(RAFTSERVER *s, RAFTPEER *from, REQUEST_VOTE_REP_RPC *rpc) {
	// rpc is too late
	if (((RPC *)rpc)->term < s->curterm)
		return;

	if (rpc->voteGranted) {
		rinfo(s, "voted from %d\n", from->peerid);
		voteme(s);
	}
}

static bool
biszero(void *buf, size_t size) {
	char zero[size];

	memset(zero, 0, size);
	return !memcmp(buf, zero, size);
}   

#define foreachLog(entry, log, n)	\
	for ((entry) = (log); (entry) < &(log)[(n)] && (entry)->term != 0; (entry)++)

static void
logDump(LOG *log, int n) {
	LOG *entry;

	printf("log dump %d:\n", n);
	foreachLog(entry, log, n) {
		// printf("log T%d %d %d\n", entry->term, entry->cmd.op, entry->cmd.arg);
		printf("log T%d %12s\n", entry->term, entry->s);
	}
}

static void
appendLog(RAFTSERVER *s, LOG *log) {
	s->logIndex++;
	s->log[s->logIndex] = *log;
}

static void
commit(RAFTSERVER *s) {
	int start;
	LOG *log;

	start = s->lastApplied + 1;
	for (int i = start; i <= s->commitIndex; i++) {
		log = s->log + i;
		// TODO: do cmd
		rinfo(s, "commit log%d! LOG{T%d:%12s}\n", i, log->term, log->s);
	}

	s->lastApplied = s->commitIndex;
}

static void
recvAppendEntries(RAFTSERVER *s, RAFTPEER *from, APPEND_ENTRIES_RPC *rpc) {
	APPEND_ENTRIES_REP_RPC reply;
	LOG *entry;
	
	if (((RPC *)rpc)->term < s->curterm)
		return;

	if (biszero(rpc->entries, sizeof rpc->entries))	// heartbeat
		return;

	logDump(rpc->entries, 32);

	reply.rpc.type = APPEND_ENTRIES_REPLY;
	reply.success = false;

	if (rpc->prevLogIndex == -1 ||
	    s->log[rpc->prevLogIndex].term == rpc->prevLogTerm) {
		reply.success = true;

		// TODO: confilict check

		foreachLog(entry, rpc->entries, 32) {
			appendLog(s, entry);
		}

		if (rpc->leaderCommit > s->commitIndex) {
			s->commitIndex = min(rpc->leaderCommit, s->logIndex);
		}
	}

	sendrpc(s, (RPC *)&reply, sizeof reply, from);
}

// if N is not found, return -1
static int
getN(RAFTSERVER *s) {
	int count;
	bool major;
	RAFTPEER *p;
	int maxN = -1;

	for (int n = s->commitIndex + 1; n <= s->logIndex; n++) {
		count = 0;
		foreachPeer(s, p) {
			if (p->matchIndex >= n) {
				count++;
				if (count * 2 > s->npeers + 1) {
					major = true;
					break;
				}
			}
		}
		// count is major and same term
		if (major && s->log[n].term == s->curterm) {
			maxN = n;
		}
	}

	return maxN;
}

static void
recvAppendEntriesRep(RAFTSERVER *s, RAFTPEER *from, APPEND_ENTRIES_REP_RPC *rpc) {
	int n;

	if (((RPC *)rpc)->term < s->curterm)
		return;

	assert(s->state == LEADER);

	if (rpc->success) {
		from->nextIndex = s->logIndex + 1;
		from->matchIndex = s->logIndex;

		/* 
		 * If there exists an N such that N > commitIndex, a majority
		 * of matchIndex[i] ≥ N, and log[N].term == currentTerm:
		 * set commitIndex = N (§5.3, §5.4)
		 */
		n = getN(s);
		if (n >= 0) {
			s->commitIndex = n;
		}
	} else {
		// resend append entries
		from->nextIndex--;

		__appendEntries(s, from, false);
	}
}

static void
recvrpc(RAFTSERVER *s, RAFTPEER *from) {
	char buf[1024] = {0};
	RPC *rpc = (RPC *)buf;
	size_t rpcsize;

	if (s->state == DEAD)
		return;

	rpcsize = readrpc(from, rpc, 1024);
	if (rpcsize == 0)
		peerDisconnect(s, from);

	if (rpc->term > s->curterm) {
		setCurTerm(s, ((RPC *)rpc)->term);
		s->state = FOLLOWER;
		s->leader = from;
		voteDone(s);
	}

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
bcastrpc(RAFTSERVER *s, RPC *rpc, size_t size) {
	RAFTPEER *peer;

	foreachPeer(s, peer) {
		sendrpc(s, rpc, size, peer);
	}
}

static void
requestVote(RAFTSERVER *s) {
	REQUEST_VOTE_RPC rpc;

	if (s->state != CANDIDATE)
		return;
	rinfo(s, "request vote\n");

	rpc.rpc.type = REQUEST_VOTE;
	rpc.candidateId = s->myid;
	rpc.lastLogIndex = s->logIndex;
	if (s->logIndex >= 0) {
		rpc.lastLogTerm = s->log[s->logIndex].term;
	} else {
		rpc.lastLogTerm = -1;
	}

	bcastrpc(s, (RPC *)&rpc, sizeof rpc);
}

static RPC *
allocrpc(RPCTYPE type, size_t size) {
	RPC *rpc;

	rpc = malloc(size);
	if (!rpc)
		return NULL;

	bzero(rpc, size);
	rpc->type = type;
	return rpc;
}

static void
__appendEntries(RAFTSERVER *s, RAFTPEER *p, bool heartbeat) {
	APPEND_ENTRIES_RPC *rpc;
	int prevLogIndex, prevLogTerm;
	int eidx = 0;

	if (s->state != LEADER) {
		return;
	}

	prevLogIndex = p->nextIndex - 1;
	if (prevLogIndex >= 0) {
		prevLogTerm = s->log[prevLogIndex].term;
	} else {
		prevLogTerm = -1;
	}

	rpc = (APPEND_ENTRIES_RPC *)allocrpc(APPEND_ENTRIES, sizeof *rpc);
	rpc->leaderId = s->myid;
	rpc->prevLogIndex = prevLogIndex;
	rpc->prevLogTerm = prevLogTerm;
	rpc->leaderCommit = s->commitIndex;

	assert(biszero(rpc->entries, sizeof rpc->entries));
	if (heartbeat) {
		goto send;
	}

	eidx = 0;
	printf("logIndex %d nI %d\n", s->logIndex, p->nextIndex);
	if (s->logIndex < p->nextIndex) {
		goto end;
	}
	for (int i = p->nextIndex; i <= s->logIndex; i++, eidx++) {
		LOG *l;

		rinfo(s, "%d: appendEntires: %d\n", eidx, i);
		if (eidx >= 32) {
			goto end;
		}
		l = rpc->entries + eidx;
		*l = s->log[i];
	}

send:
	sendrpc(s, (RPC *)rpc, sizeof *rpc, p);
end:
	free(rpc);
}

static void
appendEntries(RAFTSERVER *s) {
	RAFTPEER *p;

	foreachPeer(s, p) {
		__appendEntries(s, p, false);
	}
}

static void
sendHeartbeat(RAFTSERVER *s) {
	RAFTPEER *p;

	foreachPeer(s, p) {
		__appendEntries(s, p, true);
	}
}

static void
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
	struct timeval tv;

	high = s->htimeoutHi;
	low = s->htimeoutLo;
	range = high - low;
	if (range < 0) {
		return 0;
	}

	gettimeofday(&tv, NULL);
	srand(tv.tv_sec + tv.tv_usec);
	return low + rand() % range;
}

static int
tickinit(RAFTSERVER *s, void (*callback)(union sigval)) {
	struct sigevent se;
	struct itimerspec ts;
	timer_t timer;

	se.sigev_notify = SIGEV_THREAD;
	se.sigev_value.sival_ptr = (void *)s;
	se.sigev_notify_function = callback;
	se.sigev_notify_attributes = NULL;

	ts.it_value = ms2timespec(s->heartbeatTick);
	ts.it_interval = ms2timespec(s->heartbeatTick);

	if (timer_create(CLOCK_MONOTONIC, &se, &timer) < 0) {
		printf("timer_create!");
		return -1;
	}
	if (timer_settime(timer, 0, &ts, 0) < 0) {
		printf("timer_settime!");
		return -1;
	}

	s->timer = timer;

	return 0;
}

static RAFTPEER *
allocpeer(RAFTSERVER *s) {
	RAFTPEER *p;

	for (int i = 0; i < 8; i++) {
		p = s->peers + i;
		if (!p->active) {
			p->active = true;
			p->peerid = i;
			return p;
		}
	}

	return NULL;
}

static void
connectallpeer(RAFTSERVER *s, int nservs) {
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

		ch = tcpConnect(iplist[i], 1145, 0);
		if (ch) {
			connected |= 1 << i;

			peer = allocpeer(s);
			if (!peer) {
				printf("no peer\n");
				return;
			}

			peer->wrch = ch;
			peer->peerid = i;
			peer->addr = ch->addr;
			peeridx++;
		}

	cnctd:
		i = (i + 1) % nservs;
	} while (connected != mask);
}

static int
serverinit(RAFTSERVER *s, int myid, int nservs) {
	int sock;
	char *myip = iplist[myid];
	int port = 1145;

	bzero(s, sizeof *s);
	s->state = DEAD;
	s->htimeoutLo = 100;
	s->htimeoutHi = 300;		// heartbeat timeout is 100-300 ms
	s->heartbeatTick = 50;		// heartbeat per 50 ms
	s->npeers = 0;
	s->logIndex = -1;
	s->commitIndex = -1;
	s->lastApplied = -1;

	sock = tcpListen(myip, port);	// establish tcp connection
	if (sock < 0) {
		printf ("listen failed\n");
		return -1;
	}
	printf("listen at %s:%d...\n", myip, port);
	s->socket = sock;
	s->myid = myid;
	s->ipaddr = myip;

	connectallpeer(s, nservs);

	if (tickinit(s, heartbeat) < 0) {
		return -1;
	}

	s->state = FOLLOWER;

	return 0;
}

static void
election(RAFTSERVER *s) {
	setCurTerm(s, s->curterm + 1);

	rinfo(s, "voted to me!\n");

	voteme(s);
	s->votefor = s->myid;

	requestVote(s);
}

static void
doHeartbeatTimeout(RAFTSERVER *s) {
	if (s->state == LEADER)
		return;

	if (s->state == CANDIDATE) {
		rinfo(s, "reelection\n");
		voteDone(s);
	}

	s->state = CANDIDATE;
	election(s);
}

static void
newClient(RAFTSERVER *s, TCP *ch) {
	CLIENT *c;
	
	if (s->state != LEADER) {
		rinfo(s, "I'm not a leader! Disconnect\n");
		tcpDisconnect(ch);
		return;
	}

	c = malloc(sizeof *c);
	if (!c) {
		return;
	}

	rinfo(s, "new client!\n");

	c->ch = ch;
	if (s->client)
		printf("double!?\n");
	s->client = c;
}

static void
clientDisconnect(RAFTSERVER *s) {
	CLIENT *c = s->client;

	tcpDisconnect(c->ch);
	free(c);
	s->client = NULL;
}

static void
recvClientReq(RAFTSERVER *s) {
	CLIENT *c = s->client;
	LOG log;
	size_t n;

	if (s->state != LEADER) {
		rinfo(s, "I'm not a leader, disconnect\n");
		clientDisconnect(s);
		return;
	}
	
	n = tcpRecv(c->ch, log.s, sizeof log.s);
	if (n == 0) {
		clientDisconnect(s);
		return;
	}

	// internal command
	if (strcmp(log.s, "p") == 0) {
		logDump(s->log, 256);
		return;
	} else if (strcmp(log.s, "c") == 0) {
		logDump(s->log, s->commitIndex + 1);
		return;
	}

	log.term = s->curterm;

	logDump(&log, 1);
	appendLog(s, &log);
	appendEntries(s);
}

static int
raftpollfd(RAFTSERVER *s, struct pollfd *fds, RAFTPEER **peers) {
	int nfds;
	RAFTPEER *peer;
	CLIENT *c = s->client;
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

	if (c) {
		fds[nfds++] = (struct pollfd){ .fd = c->ch->fd, .events = POLLIN };
	}

	return nfds;
}

static void
peerReconnected(RAFTSERVER *s, TCP *ch) {
	RAFTPEER *p;
	TCP *wrch;

	p = allocpeer(s);
	if (!p) {
		return;
	}

	rinfo(s, "peer reconnected! from %d:%s\n", p->peerid, iplist[p->peerid]);

	wrch = tcpConnect(iplist[p->peerid], 1145, 0);
	if (!wrch) {
		return;
	}

	p->wrch = wrch;
	p->rdch = ch;
	p->addr = ch->addr;

	s->npeers++;
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

	commit(s);

	nfds = raftpollfd(s, fds, peers);
	nready = poll(fds, nfds, timeout);
	if (nready < 0) {
		perror("poll");
		return -1;
	}
	if (!nready) {
		// rinfo(s, "timeout: %d ms\n", timeout);
		doHeartbeatTimeout(s);
		return 0;
	}

	for (int i = 0; i < nfds && nready; i++) {
		pfd = &fds[i];
		if (!(pfd->revents & POLLIN))
			continue;
		
		if (pfd->fd == s->socket) {
			rdch = tcpAccept(s->socket);
			if (!rdch) {
				printf("accept failed\n");
				return -1;
			}

			// from client
			if (ntohs(rdch->addr.sin_port) == 1919) {
				newClient(s, rdch);
			} else {
				peer = peerbyip(s, rdch->addr);
				if (peer) {
					peer->rdch = rdch;
					rinfo(s, "new peer!: %d\n", peer->peerid);
				} else {
					peerReconnected(s, rdch);
				}
			}
		} else if (s->client && pfd->fd == s->client->ch->fd) {
			recvClientReq(s);
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

	if (argc < 2)
		return -1;
	me = atoi(argv[1]);

	signal(SIGPIPE, SIG_IGN);

	rc = serverinit(&server, me, N);
	if (rc) {
		return rc;
	}
	
	for (;;) {
		rc = servermain(&server);
		if (rc) {
			return rc;
		}
	}
}
