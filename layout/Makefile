CC=gcc
SAN=-fsanitize=address,undefined -fno-omit-frame-pointer
CFLAGS=-std=c99 -Wall -Wextra -Werror -g $(SAN) -I../src

HDRS=../src/layout.h ../src/desk.h ../src/action.h ../src/compat.h ../src/keys.h ../src/bind.h ../src/input.h ../src/wctl.h ../src/wq.h ../src/tkeys.h ../src/tbuf.h ../src/ctl.h ../src/rule.h ../src/ev.h script.h
PURE=layout desk action bind input wctl wq tkeys tbuf ctl rule ev
CORE=../src/action.c ../src/layout.c ../src/desk.c
KBD=../src/input.c ../src/bind.c

layouttest: test.c $(CORE) $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^)

kbdtest: kbdtest.c $(KBD) $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^)

wintest: wintest.c ../src/wctl.c ../src/wq.c ../src/ctl.c $(CORE) $(KBD) $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^)

termtest: termtest.c ../src/tkeys.c ../src/tbuf.c $(KBD) $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^)

ctltest: ctltest.c ../src/ctl.c ../src/rule.c ../src/ev.c $(CORE) $(KBD) $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^)

test: layouttest kbdtest wintest termtest ctltest
	./run-tests.sh
	! grep -nE '#include[[:space:]]*<(draw|thread|9p|mouse|keyboard|frame)\.h>' $(PURE:%=../src/%.c) $(PURE:%=../src/%.h)
	! grep -nE '#include[[:space:]]*<' $(PURE:%=../src/%.c)

clean:
	rm -f layouttest kbdtest wintest termtest ctltest

.PHONY: test clean
