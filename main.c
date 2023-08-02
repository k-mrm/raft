#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include <signal.h>
#include <sys/timerfd.h>
#include <poll.h>
#include "tcp.h"

enum state {
	NONESTATE,
	FOLLOWER,
	CANDIDATE,
	LEADER,
};

struct raftpeer {
	struct tcpchnl *ch;

	int peerid;

	bool active;
};

struct raftserver {
	enum state state;

	int htimeout_lo;
	int htimeout_hi;

	int socket;
	int port;

	struct raftpeer peers[8];
	int npeers;

	int myid;	

	timer_t timer;
	int heartbeat_tick;

	int term;

	int votes;
};

enum rpctype {
	REQUEST_VOTE,
	APPEND_ENTRIES,
};

struct rpc {
	enum rpctype type;
	int term;
};

struct request_vote_rpc {
	struct rpc rpc;
	int candidate_id;
	int last_logindex;
	int last_logterm;
};

struct request_vote_rep_rpc {
	struct rpc rpc;
	bool vote_granted;
};

struct append_entries_rpc {
	struct rpc rpc;
	int leaderid;
	int prev_logindex;
	int prev_logterm;
	char entries[32];
	int leader_commit;
} __attribute__((packed));

struct append_entries_rep_rpc {
	struct rpc rpc;
	bool success;
} __attribute__((packed));

static struct raftpeer *
peerbyid(struct raftserver *s, int peerid) {
	struct raftpeer *p = NULL;

	for (int i = 0; i < s->npeers; i++) {
		p = s->peers + i;
		if (p->peerid == peerid)
			break;
	}
	return p;
}

static void
sendrpc(struct raftserver *s, struct rpc *rpc) {
	;
}

static void
recvrpc(struct raftserver *s, struct rpc *rpc) {
	;
}

static void
send_heartbeat(struct raftserver *s) {
	struct rpc rpc;
	rpc.type = APPEND_ENTRIES;

	sendrpc(s, &rpc);
}

void
heartbeat(union sigval sv) {
	struct raftserver *s = sv.sival_ptr;

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
heartbeat_timeout(struct raftserver *s) {
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
tickinit(struct raftserver *s, void (*callback)(union sigval)) {
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
connectserv(struct raftserver *s, int *servids, int nservs) {
	int peeridx = 0;
	int i = 0;
	struct raftpeer *peer;
	struct tcpchnl *chnl;
	int mask = (1 << nservs) - 1;
	int connected = 0;

	s->npeers = nservs - 1;

	do {
		if (connected & (1 << i))
			goto cnctd;

		usleep(100 * 1000);	// wait 100ms

		if (servids[i] == s->myid) {
			connected |= 1 << i;
			goto cnctd;
		}

		peer = &s->peers[peeridx];
		chnl = connect_tcp("0.0.0.0", servids[i]);
		if (chnl) {
			connected |= 1 << i;
			peer->ch = chnl;
			peer->peerid = servids[i];
			peer->active = true;
		}

cnctd:
		i = (i + 1) % nservs;
	} while (connected != mask);
}

static void
serverinit(struct raftserver *s, int me, int *servids, int nservs) {
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

	connectserv(s, servids, nservs);

	tickinit(s, heartbeat);

	printf("serverinitdone\n");
}

static void
raftlog(struct raftserver *s, const char *fmt, ...) {
	;
}

static int
servermain(struct raftserver *s) {
	int nready;
	struct pollfd fds[16];
	struct pollfd *pfd;
	int nfds;
	int timeout = heartbeat_timeout(s);

	fds[0] = (struct pollfd){ .fd = s->socket, .events = POLLIN };
	nfds = 1;

	nready = poll(fds, nfds, timeout);
	if (!nready) {
		// printf("timeout\n");
		return 0;
	}

	for (int i = 0; i < nfds && nready; i++) {
		pfd = &fds[i];
		if (!(pfd->revents & POLLIN))
			continue;
		
		if (pfd->fd == s->socket) {
			struct tcpchnl *peer = tcp_accept(s->socket);
			if (!peer)
				return -1;
			printf("!! new node\n");
		}
		nready--;
	}

	return 0;
}

static struct raftnode *
raft_leader(struct raftserver *s) {
	;
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
	struct raftserver server;
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
