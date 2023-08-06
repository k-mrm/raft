#ifndef __RAFT_LOG_H
#define __RAFT_LOG_H

typedef enum VAR VAR;
enum VAR {
	X,
	Y,
	Z,
};

typedef enum OP OP;
enum OP {
	SET,
	ADD,
	SUB,
};

typedef struct COMMAND COMMAND;
struct COMMAND {
	VAR var;
	OP op;
	int arg;	
};

typedef struct LOG LOG;
struct LOG {
	int term;
	// COMMAND cmd;
	char s[12];	// tmp
};

#endif	/* __RAFT_LOG_H */
