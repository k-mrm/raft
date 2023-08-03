CC = gcc
LD = ld

CFLAGS += -Wall -Wextra -I ./ -Og -g

TARGET = raft
OBJS = main.o tcp.o

.PHONY: all clean

all: $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(COBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) *.out $(OBJS) $(TARGET)
