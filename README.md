# raft
なんちゃってraft

## How to Use
- make
```
$ make raft
```

- prepare environment (require sudo)
```
$ sh demons.sh
```

- execution (require sudo)
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

  - client
```
# in console 4
$ ./client <leader's ip>
```
