#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include <signal.h>
#include <sys/timerfd.h>
#include "tcp.h"

enum state {
	FOLLOWER,
	CANDIDATE,
	LEADER,
};

struct raftserver {
	enum state state;
	int htimeout_lo;
	int htimeout_hi;
	int heartbeat_tick;
	struct tcpchnl *nodes[8];
	int nodeid;	
	timer_t timer;
	pthread_t cb_thread;
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

	printf ("heartbeat!\n");
}

static struct timespec
ms_to_timespec(int ms) {
	struct timespec ts;
	ts.tv_sec = ms / 1000;	
	ts.tv_nsec = (ms % 1000) * 1000000;
	return ts;
}

static void
tickinit(struct raftserver *s, void (*callback)(union sigval)) {
	struct sigevent se;
	struct itimerspec ts;
	timer_t timer;
	int status;

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
}

static void
serverinit(struct raftserver *s) {
	s->state = FOLLOWER;
	s->htimeout_lo = 150;
	s->htimeout_hi = 300;	// heartbeat timeout is 150-300 ms
	s->heartbeat_tick = 50;	// heartbeat per 50 ms

	tickinit(s, heartbeat);
}

static int
servermain(struct raftserver *s) {
	return 0;
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

int
main(void) {
	struct raftserver server;

	serverinit(&server);
	return servermain(&server);
}
