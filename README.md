# raft
なんちゃってraft

## How to Use
- make
```
$ make
```

- prepare environment (require sudo)
```
$ ./demons.sh
```

- execution
```
# in console 1
$ ./raft 0
# in console 2
$ sudo ip netns exec host1 bash
$ ./raft 1
# in console 3
$ sudo ip netns exec host2 bash
$ ./raft 2
```
