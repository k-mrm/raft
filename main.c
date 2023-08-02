#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include <signal.h>
#include <sys/timerfd.h>
#include <assert.h>
#include <poll.h>
#include "tcp.h"

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

	int peerid;

	bool active;
};

typedef struct RAFTSERVER RAFTSERVER;
struct RAFTSERVER {
	RSTATE state;

	int htimeout_lo;
	int htimeout_hi;

	int socket;
	int port;

	RAFTPEER peers[8];
	int npeers;

	int myid;	

	timer_t timer;
	int heartbeat_tick;

	int term;

	int votes;
};

typedef enum RPCTYPE RPCTYPE;
enum RPCTYPE {
	REQUEST_VOTE,
	APPEND_ENTRIES,
};

typedef struct RPC RPC;
struct RPC {
	RPCTYPE type;
	int term;
};

typedef struct REQUEST_VOTE_RPC REQUEST_VOTE_RPC;
struct REQUEST_VOTE_RPC {
	RPC rpc;
	int candidate_id;
	int last_logindex;
	int last_logterm;
};

typedef struct REQUEST_VOTE_REP_RPC REQUEST_VOTE_REP_RPC;
struct REQUEST_VOTE_REP_RPC {
	RPC rpc;
	bool vote_granted;
};

typedef struct APPEND_ENTRIES_RPC APPEND_ENTRIES_RPC;
struct APPEND_ENTRIES_RPC {
	RPC rpc;
	int leaderid;
	int prev_logindex;
	int prev_logterm;
	char entries[32];
	int leader_commit;
} __attribute__((packed));

typedef struct APPEND_ENTRIES_REP_RPC APPEND_ENTRIES_REP_RPC;
struct APPEND_ENTRIES_REP_RPC {
	RPC rpc;
	bool success;
} __attribute__((packed));

static RAFTPEER *
peerbyid(RAFTSERVER *s, int peerid) {
	RAFTPEER *p;

	for (int i = 0; i < s->npeers; i++) {
		p = s->peers + i;
		if (p->peerid == peerid)
			return p;
	}

	return NULL;
}

static void
sendrpc(RAFTSERVER *s, RPC *rpc, size_t size, RAFTPEER *target) {
	rpc->term = s->term;

	tcp_write(target->wrch, rpc, size);
}

static void
recvrpc(RAFTSERVER *s, RPC *rpc) {
}

static void
requestVote(RAFTSERVER *s) {
	REQUEST_VOTE_RPC rpc;
	RAFTPEER *peer;

	assert(s->state == CANDIDATE);
	printf("request vote\n");

	rpc.rpc.type = REQUEST_VOTE;
	rpc.candidate_id = s->myid;
	rpc.last_logindex = 0;
	rpc.last_logterm = 0;

	// broadcast
	for (int i = 0; i < s->npeers; i++) {
		peer = s->peers + i;
		sendrpc(s, (RPC *)&rpc, sizeof rpc, peer);
	}
}

static void
send_heartbeat(RAFTSERVER *s) {
	RPC rpc;
	rpc.type = APPEND_ENTRIES;
}

void
heartbeat(union sigval sv) {
	RAFTSERVER *s = sv.sival_ptr;

	if (s->state == LEADER) {
		send_heartbeat(s);
	}
	// printf ("heartbeat!\n");
}

static struct timespec
ms_to_timespec(int ms) {
	struct timespec ts;
	ts.tv_sec = ms / 1000;	
	ts.tv_nsec = (ms % 1000) * 1000000;
	return ts;
}

static int
heartbeat_timeout(RAFTSERVER *s) {
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

	ts.it_value = ms_to_timespec(s->heartbeat_tick);
	ts.it_interval = ms_to_timespec(s->heartbeat_tick);

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
connectallserv(RAFTSERVER *s, int *servids, int nservs) {
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

		if (servids[i] == s->myid) {
			connected |= 1 << i;
			goto cnctd;
		}

		peer = &s->peers[peeridx];
		ch = connect_tcp("0.0.0.0", servids[i]);
		if (ch) {
			connected |= 1 << i;
			peer->wrch = ch;
			peer->peerid = servids[i];
			peer->active = true;
			peeridx++;
		}

cnctd:
		i = (i + 1) % nservs;
	} while (connected != mask);
}

static void
serverinit(RAFTSERVER *s, int me, int *servids, int nservs) {
	int sock;

	s->state = NONESTATE;
	s->htimeout_lo = 150;
	s->htimeout_hi = 300;		// heartbeat timeout is 150-300 ms
	s->heartbeat_tick = 50;		// heartbeat per 50 ms

	sock = tcp_listen(me);	// establish tcp connection
	if (sock < 0) {
		printf ("listen failed\n");
		return;
	}
	printf("listen at %d...\n", me);
	s->socket = sock;
	s->port = me;
	s->myid = me;

	connectallserv(s, servids, nservs);

	tickinit(s, heartbeat);

	s->state = FOLLOWER;

	printf("serverinitdone\n");
}

static void
raftlog(RAFTSERVER *s, const char *fmt, ...) {
	;
}

static void
do_heartbeat_timeout(RAFTSERVER *s) {
	if (s->state != FOLLOWER)
		return;

	printf("timeout: follower -> candidate\n");
	s->state = CANDIDATE;
	s->term++;

	requestVote(s);
}

static int
servermain(RAFTSERVER *s) {
	int nready;
	struct pollfd fds[16];
	struct pollfd *pfd;
	RAFTPEER *peer;
	TCP *rdch;
	int nfds;
	int timeout = heartbeat_timeout(s);

	fds[0] = (struct pollfd){ .fd = s->socket, .events = POLLIN };
	nfds = 1;

	nready = poll(fds, nfds, timeout);
	if (!nready) {
		do_heartbeat_timeout(s);
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

			// peerid == port number
			peer = peerbyid(s, rdch->port);
			if (!peer) {
				printf("no peer!\n");
				return -1;
			}
			peer->rdch = rdch;
			printf("new peer!: %d\n", peer->peerid);
		} else {
			printf("message from other!\n");
		}

		nready--;
	}

	return 0;
}

static RAFTPEER *
raft_leader(RAFTSERVER *s) {
	// TODO
	return NULL;
}

static int
serveridinit(char **servs, int nservs, int *servids) {
	int id;

	for (int i = 0; i < nservs; i++) {
		id = atoi(servs[i]);
		if (!id)
			return -1;
		servids[i] = id;
	}

	return 0;
}

/* servid == port number */
int
main(int argc, char *argv[]) {
	RAFTSERVER server;
	int ids[8];
	int rc, me, nsids;
	char **servs;

	if (argc < 3)
		return -1;
	me = atoi(argv[1]);
	if (!me)
		return -1;

	// ./raft 1 1145 1919 931
	nsids = argc - 2;
	servs = argv + 2;

	if (serveridinit(servs, nsids, ids) < 0)
		return -1;
	serverinit(&server, ids[me - 1], ids, nsids);
	
	for (;;) {
		rc = servermain(&server);
		if (rc)
			return rc;
	}
}
