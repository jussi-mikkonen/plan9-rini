# kbd, K on unfocus and bounded queue
q 0 init
q 0 open kbd
q 0 kbd k 61
q 0 kbd c 61
q 0 kbd K
q 0 read kbd 64
q 0 read kbd 64
# unfocus ends the next kbd read with a bare K
q 1 init
q 1 open kbd
q 1 kbd k 61
q 1 unfocus
q 1 read kbd 64
# kbd closed: unfocus sets no keyup
q 2 init
q 2 unfocus
q 2 open kbd
q 2 read kbd 64
# bounded: 40 messages, 32 kept
q 3 init
q 3 open kbd
q 3 kbd k 61
q 3 kbd k 61
q 3 kbd k 61
q 3 kbd k 61
q 3 kbd k 61
q 3 kbd k 61
q 3 kbd k 61
q 3 kbd k 61
q 3 kbd k 61
q 3 kbd k 61
q 3 kbd k 61
q 3 kbd k 61
q 3 kbd k 61
q 3 kbd k 61
q 3 kbd k 61
q 3 kbd k 61
q 3 kbd k 61
q 3 kbd k 61
q 3 kbd k 61
q 3 kbd k 61
q 3 kbd k 61
q 3 kbd k 61
q 3 kbd k 61
q 3 kbd k 61
q 3 kbd k 61
q 3 kbd k 61
q 3 kbd k 61
q 3 kbd k 61
q 3 kbd k 61
q 3 kbd k 61
q 3 kbd k 61
q 3 kbd k 61
q 3 kbd k 61
q 3 kbd k 61
q 3 kbd k 61
q 3 kbd k 61
q 3 kbd k 61
q 3 kbd k 61
q 3 kbd k 61
q 3 kbd k 61
q 3 read kbd 512
