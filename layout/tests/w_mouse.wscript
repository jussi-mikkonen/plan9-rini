# mouse queue, counter, delta and warp
q 0 init
q 0 open mouse
q 0 mouse 150 50 0 10
q 0 read mouse
q 0 read mouse
# delta translates screen to window coordinates
q 1 init
q 1 open mouse
q 1 delta 100 0
q 1 mouse 150 50 1 20
q 1 read mouse
# reshape: one r read, then nothing until a real event
q 2 init
q 2 open mouse
q 2 reshape
q 2 read mouse
q 2 read mouse
q 2 mouse 7 8 0 30
q 2 read mouse
q 2 read mouse
# unfocus with buttons held: one buttons-up read, then nothing
q 2 init
q 2 open mouse
q 2 mouse 7 8 1 40
q 2 read mouse
q 2 unfocus
q 2 read mouse
q 2 read mouse
# a move changes only the delta: nothing to read
q 3 init
q 3 open mouse
q 3 delta 300 0
q 3 read mouse
# bounded queue: 20 press/release pairs, 16 kept in order, then the current state
q 0 init
q 0 open mouse
q 0 mouse 1 0 1 1
q 0 mouse 2 0 0 2
q 0 mouse 3 0 1 3
q 0 mouse 4 0 0 4
q 0 mouse 5 0 1 5
q 0 mouse 6 0 0 6
q 0 mouse 7 0 1 7
q 0 mouse 8 0 0 8
q 0 mouse 9 0 1 9
q 0 mouse 10 0 0 10
q 0 mouse 11 0 1 11
q 0 mouse 12 0 0 12
q 0 mouse 13 0 1 13
q 0 mouse 14 0 0 14
q 0 mouse 15 0 1 15
q 0 mouse 16 0 0 16
q 0 mouse 17 0 1 17
q 0 mouse 18 0 0 18
q 0 mouse 19 0 1 19
q 0 mouse 20 0 0 20
q 0 read mouse
q 0 read mouse
q 0 read mouse
q 0 read mouse
q 0 read mouse
q 0 read mouse
q 0 read mouse
q 0 read mouse
q 0 read mouse
q 0 read mouse
q 0 read mouse
q 0 read mouse
q 0 read mouse
q 0 read mouse
q 0 read mouse
q 0 read mouse
q 0 read mouse
q 0 read mouse
# events while closed are not queued
q 1 init
q 1 mouse 5 5 1 1
q 1 open mouse
q 1 read mouse
# warp
q 2 init
q 2 delta 100 5
q 2 warp m 10 20
q 2 warp x10 20
q 2 warp m10
q 2 warp m 10 -3
q 2 warp m1234567890 5
q 2 warp m-3000000000 0
