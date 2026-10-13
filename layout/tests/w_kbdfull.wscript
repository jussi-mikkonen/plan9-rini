# a full kbd queue drops the oldest k or K, so the last key state survives
q 0 init
q 0 open kbd
q 0 kbd k 61
q 0 kbd k 61
q 0 kbd k 61
q 0 kbd k 61
q 0 kbd k 61
q 0 kbd k 61
q 0 kbd k 61
q 0 kbd k 61
q 0 kbd k 61
q 0 kbd k 61
q 0 kbd k 61
q 0 kbd k 61
q 0 kbd k 61
q 0 kbd k 61
q 0 kbd k 61
q 0 kbd k 61
q 0 kbd k 61
q 0 kbd k 61
q 0 kbd k 61
q 0 kbd k 61
q 0 kbd k 61
q 0 kbd k 61
q 0 kbd k 61
q 0 kbd k 61
q 0 kbd k 61
q 0 kbd k 61
q 0 kbd k 61
q 0 kbd k 61
q 0 kbd k 61
q 0 kbd k 61
q 0 kbd k 61
q 0 kbd k 61
q 0 kbd K
q 0 read kbd 512
# full of c: another c is dropped itself, a K drops the oldest c
q 1 init
q 1 open kbd
q 1 kbd c 62
q 1 kbd c 62
q 1 kbd c 62
q 1 kbd c 62
q 1 kbd c 62
q 1 kbd c 62
q 1 kbd c 62
q 1 kbd c 62
q 1 kbd c 62
q 1 kbd c 62
q 1 kbd c 62
q 1 kbd c 62
q 1 kbd c 62
q 1 kbd c 62
q 1 kbd c 62
q 1 kbd c 62
q 1 kbd c 62
q 1 kbd c 62
q 1 kbd c 62
q 1 kbd c 62
q 1 kbd c 62
q 1 kbd c 62
q 1 kbd c 62
q 1 kbd c 62
q 1 kbd c 62
q 1 kbd c 62
q 1 kbd c 62
q 1 kbd c 62
q 1 kbd c 62
q 1 kbd c 62
q 1 kbd c 62
q 1 kbd c 62
q 1 kbd c 63
q 1 kbd K
q 1 read kbd 512
# the focus-loss K queues in order: keys pressed after it come after it
q 2 init
q 2 open kbd
q 2 kbd k 61
q 2 unfocus
q 2 kbd k 77
q 2 read kbd 64
