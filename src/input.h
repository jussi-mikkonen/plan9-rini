/* input.h: kbd message router. */
#ifndef INPUT_H
#define INPUT_H

#include "bind.h"

#define KMSGLEN 128
#define MAXHELD 16
#define MAXOUT 16
#define KQ 32

typedef struct Kout Kout;
typedef struct Swal Swal;
typedef struct Input Input;
typedef struct Kq Kq;

struct Kout {
	char msg[KMSGLEN];	/* the one forwarded message, not NUL-terminated */
	int len;	/* 0: none */
	int nact;
	char act[MAXOUT][ACTLEN];
};

/* a copy, not a Binding*: unbind may shift or remove the entry while the key is held */
struct Swal {
	int key;
	int repeat;
	int cseen;
	char action[ACTLEN];
};

struct Input {
	Bindtab *bt;
	int held[MAXHELD];
	int nheld;
	int fwd[MAXHELD];
	int nfwd;
	Swal sw[MAXHELD];
	int nsw;
	int last;	/* the last pressed plain key still held: kbdfs cooks and repeats only that one */
	int armed;
	long armedat;	/* ms */
	int leaderdown;
	int leaderused;
	int gen;	/* Bindtab.gen the leader state belongs to */
};

/* ring of kbd messages, each stored with a NUL */
struct Kq {
	char m[KQ][KMSGLEN];
	int len[KQ];	/* bytes including the NUL */
	int h, n;
};

void input_init(Input*, Bindtab*);
/* len excludes the NUL, msec is the time; 0 ok, -1 malformed (dropped) */
int input_kbd(Input*, char *msg, int len, long msec, Kout*);
/* is rune (a key constant) currently held */
int input_heldkey(Input*, int rune);
/* configured WM modifier held (mask mode) or leader down (leader mode) */
int input_modheld(Input*);
/* bytes consumed or -1 */
int input_utf8dec(char *s, int n, int *rune);
/* bytes written or -1 */
int input_utf8enc(int rune, char *buf, int n);
/* msg without NUL; -1 when too long, or when full and msg is the one dropped (input.c) */
int kq_put(Kq*, char *msg, int len);
/* the head message plus NUL into buf, returns its length; 0 empty; -1 when n is too small */
int kq_get(Kq*, char *buf, int n);

#endif
