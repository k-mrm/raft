CC = gcc
LD = ld

CFLAGS += -Wall -Wextra -I ./ -Og -g

TARGET = raft
OBJS = main.o tcp.o
COBJS = client.o tcp.o

.PHONY: all clean

raft: $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

client: $(COBJS)
	$(CC) $(CFLAGS) -o client $(COBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) *.out $(OBJS) $(COBJS) $(TARGET) client
