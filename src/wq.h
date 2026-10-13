/* wq.h: per-window mouse and kbd state and mouse routing. Pure. */
#ifndef WQ_H
#define WQ_H

#include "layout.h"
#include "input.h"

#define MOUSEQ 16
#define MOUSELEN 49

typedef struct Wmouse Wmouse;
typedef struct Wq Wq;
typedef struct Mroute Mroute;
typedef struct Mdec Mdec;
typedef struct Mact Mact;
typedef struct Mpol Mpol;

enum { Dpass, Dswallow, Dmenu, Dfmove, Dfresize, Dwheel };
enum { Wws, Wcol };	/* wheel kinds */

struct Wmouse {
	int x, y, buttons;
	long msec;
	int counter;
};

struct Wq {
	Wmouse mq[MOUSEQ];
	int mqh, mqn;
	Wmouse cur;
	int counter, lastcounter, lastb;
	int resized, mouseopen;
	int dx, dy;
	Kq kq;
	int kbdopen;
	int wctlready;
};

struct Mroute {
	int grab;
	int swallow;
	int lastb;
};

struct Mdec {
	int to;
	int focus;
};

/* corner 0..3 = top-left, top-right, bottom-left, bottom-right;
 * dx, dy the drag offset from the press point */
struct Mact {
	int kind, id, dir, corner, dx, dy;
};

struct Mpol {
	int state, id, px, py, lastb;
	long lastwheel;
	int border;
};

void wq_mouseopen(Wq*, int on);
void wq_kbdopen(Wq*, int on);
/* screen coordinates; ignored unless the mouse is open */
void wq_mouse(Wq*, int x, int y, int buttons, long msec);
int wq_mouseready(Wq*);
/* buf holds MOUSELEN+1, returns MOUSELEN */
int wq_mouseread(Wq*, char *buf);
/* parses "m<x> <y>"; returns 0 and the screen point, -1 on a bad format */
int wq_warp(Wq*, char *data, int n, int *x, int *y);
void wq_moved(Wq*, int dx, int dy);
void wq_reshaped(Wq*);
void wq_unfocus(Wq*);
/* msg is a k, K or c message without NUL */
void wq_kbd(Wq*, char *msg, int len);
int wq_kbdready(Wq*);
int wq_kbdread(Wq*, char *buf, int n);
/* x, y viewport relative; focus is the focused id or 0 */
void mroute(Mroute*, Place *p, int n, int focus, int x, int y, int buttons, Mdec *d);
/* last (topmost) entry containing the point, -1; skips parked and empty places */
int place_hit(Place*, int n, int x, int y);
/* resync after a blocking loop consumed the release; a held button is swallowed */
void mroute_reset(Mroute*, int buttons);
/* mouse policy for Mod gestures, the menu and the wheel; Dpass leaves the event to mroute */
void mpol(Mpol*, Place *p, int n, int x, int y, int buttons, long msec, int mod, int shift, Mact*);
void mpol_reset(Mpol*, int buttons);

#endif
