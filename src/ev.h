/* ev.h: event lines, snapshot diff, bounded reader queues, kbdtap queue. Pure. */
#ifndef EV_H
#define EV_H

#include "desk.h"
#include "input.h"

#define NEVQ 128	/* lines per reader queue */
#define EVLINE 256	/* max bytes per event line incl. newline; longer labels are truncated by ctl_quote */
#define EVLABEL 128

enum { Ktile, Kfloat, Khid };

typedef struct Snapwin Snapwin;
typedef struct Snap Snap;
typedef struct Evq Evq;
typedef struct Tapq Tapq;

/* ws 0-based; ignored when Khid; wmil 0 for floats and hidden */
struct Snapwin {
	int id;
	int ws;
	int kind;
	int wmil;
	char label[EVLABEL];
};

/* about 35 KB: always static, never on a libthread stack */
struct Snap {
	Snapwin w[MAXWIN];
	int n;
	int nws;
	int cur;
	int focus;
};

/* ring of n lines from h, each with its '\n' and a NUL; off = bytes of the
 * head line already read */
struct Evq {
	char l[NEVQ][EVLINE+1];
	int h, n;
	int off;
};

/* line without '\n' */
void evq_push(Evq*, char *line);
/* 0 when empty; else 1..n bytes, never past the head line's '\n' */
int evq_read(Evq*, char *buf, int n);
void ev_diff(Snap *old, Snap *new, void (*emit)(char*, void*), void *arg);
/* ev_diff from an empty Snap (nws 0, cur -1, focus -1) */
void ev_snapshot(Snap*, void (*emit)(char*, void*), void *arg);
int ev_statefmt(Snap*, char *buf, int n);

struct Tapq {
	Kq q;
	int lastid;	/* the window of the last push */
	int from;	/* the window the last read message was typed in */
};

void tap_init(Tapq*);
/* a z<id> first when focusid changed; -1 when msg is dropped (kq_put's full-queue rule) */
int tap_push(Tapq*, int focusid, char *msg, int len);
/* kq_get, and a z<id> read sets from */
int tap_read(Tapq*, char *buf, int n);
/* write-back: n < 2 -> "malformed key"; a message starting with NUL -> "null message type";
 * more than max -> "too many messages"; z messages skipped; returns the count, or -1 with err set */
int tap_split(char *d, int n, char **msg, int *len, int max, char *err, int nerr);

#endif
