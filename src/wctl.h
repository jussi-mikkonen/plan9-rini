/* wctl.h: rio wctl grammar, attach specs and the 72-byte read text. Pure. */
#ifndef WCTL_H
#define WCTL_H

#include "layout.h"

typedef struct Wctl Wctl;

enum Wcmd {
	Wnew, Wresize, Wmove, Wscroll, Wnoscroll, Wset,
	Wtop, Wbottom, Wcurrent, Whide, Wunhide, Wdelete
};

enum {
	Pr = 1<<0, Pdx = 1<<1, Pdy = 1<<2,
	Pminx = 1<<3, Pminy = 1<<4, Pmaxx = 1<<5, Pmaxy = 1<<6,
	Phide = 1<<7, Pscroll = 1<<8, Pnoscroll = 1<<9,
	Pcd = 1<<10, Ppid = 1<<11, Pid = 1<<12
};

enum { Anone, Aid, Anew, Actl };

#define WCTLLEN 72
#define WMINW 100
#define WMINH 64

/* rel has a P bit set when that value carried an explicit sign */
struct Wctl {
	int cmd;
	int set;
	int rel;
	int r[4];
	int dx, dy;
	int minx, miny, maxx, maxy;
	int pid, id;
	char *cd;	/* cd and rest point into the caller's buffer, nil when absent */
	char *rest;
};

/* tokenises s in place; 0 ok, -1 with a message in err */
int wctl_parse(char *s, Wctl *w, char *err, int nerr);
/* Anone, Aid (id set), Anew (w filled), Actl or -1 with a message in err; aname is modified */
int wctl_attach(char *aname, Wctl *w, int *id, char *err, int nerr);
/* returns bytes written, 72 for words of at most 11 chars */
int wctl_fmt(char *buf, int n, int x0, int y0, int x1, int y1, char *s1, char *s2);
/*
 * Applies -r, -min*, -max*, -dx/-dy to base in rio's set() sign rules (fixed order), keeps
 * a WMINW x WMINH minimum (the size of base for move) and shifts the result onto scr.
 * Never fails.
 */
void wctl_rect(Wctl *w, LRect base, LRect scr, LRect *out);
/* pixel width a new window asks for with -r, -dx, -minx or -maxx; 0 = none given */
int wctl_newwidth(Wctl *w, LRect scr);
char *wctl_cmdname(int cmd);

#endif
