#ifndef __RAFT_CMD_H
#define __RAFT_CMD_H

typedef enum OP OP;
enum OP {
	SET,
	ADD,
	SUB,
};

typedef struct COMMAND COMMAND;
struct COMMAND {
	char var;	// a-z
	OP op;
	int arg;	
};

#endif	/* __RAFT_CMD_H */
