/*
 * wctl grammar and rect arithmetic (rio wctl.c parsewctl).  Unlike rio, which applies
 * the flags in command-line order, wctl_rect applies them in a fixed order: -r, then
 * -minx/-miny, then -maxx/-maxy, then -dx/-dy.  The Wctl struct does not keep the order;
 * the results differ only for contrived combinations such as -dx before -minx.
 */
#include "compat.h"
#include "action.h"
#include "ctl.h"
#include "wctl.h"

/* indexed by enum Wcmd */
static char *cmds[] = {
	"new", "resize", "move", "scroll", "noscroll", "set",
	"top", "bottom", "current", "hide", "unhide", "delete",
	nil
};

enum { Kflag, Knum, Knum4, Kword };

typedef struct Flag Flag;
struct Flag {
	char *name;
	int bit;
	int kind;
};

static Flag flags[] = {
	{"-r", Pr, Knum4},
	{"-dx", Pdx, Knum},
	{"-dy", Pdy, Knum},
	{"-minx", Pminx, Knum},
	{"-miny", Pminy, Knum},
	{"-maxx", Pmaxx, Knum},
	{"-maxy", Pmaxy, Knum},
	{"-hide", Phide, Kflag},
	{"-scroll", Pscroll, Kflag},
	{"-noscroll", Pnoscroll, Kflag},
	{"-cd", Pcd, Kword},
	{"-pid", Ppid, Knum},
	{"-id", Pid, Knum},
	{nil, 0, 0}
};

char *
wctl_cmdname(int cmd)
{
	return cmds[cmd];
}

static int
badarg(char *arg, char *err, int nerr)
{
	lsnprint(err, nerr, "bad argument: %s", arg);
	return -1;
}

/* the value token after flag f, parsed into *v; a sign sets f's rel bit */
static int
num(char **pp, Wctl *w, Flag *f, int *v, char *err, int nerr)
{
	char *t;

	t = ctl_word(pp);
	if(t == nil)
		return badarg(f->name, err, nerr);
	if(action_parsenum(t, v) < 0)
		return badarg(t, err, nerr);
	if(*t == '+' || *t == '-')
		w->rel |= f->bit;
	return 0;
}

/* the destination field of a number flag */
static int *
numfield(Wctl *w, int bit)
{
	switch(bit){
	case Pdx: return &w->dx;
	case Pdy: return &w->dy;
	case Pminx: return &w->minx;
	case Pminy: return &w->miny;
	case Pmaxx: return &w->maxx;
	case Pmaxy: return &w->maxy;
	case Ppid: return &w->pid;
	}
	return &w->id;
}

int
wctl_parse(char *s, Wctl *w, char *err, int nerr)
{
	char *t, *p;
	Flag *f;
	int i;

	memset(w, 0, sizeof *w);
	p = s;
	t = ctl_word(&p);
	if(t == nil){
		lsnprint(err, nerr, "bad wctl command: ");
		return -1;
	}
	for(i = 0; cmds[i] != nil; i++)
		if(strcmp(t, cmds[i]) == 0)
			break;
	if(cmds[i] == nil){
		lsnprint(err, nerr, "bad wctl command: %s", t);
		return -1;
	}
	w->cmd = i;
	while(*(t = ctl_skip(p)) != '\0'){
		if(w->cmd == Wnew && *t != '-'){
			/* not a flag: the command text, the rest of the line */
			w->rest = t;
			return 0;
		}
		t = ctl_word(&p);
		for(f = flags; f->name != nil; f++)
			if(strcmp(t, f->name) == 0)
				break;
		if(f->name == nil){
			lsnprint(err, nerr, "bad wctl parameter: %s", t);
			return -1;
		}
		w->set |= f->bit;
		switch(f->kind){
		case Knum4:
			for(i = 0; i < 4; i++)
				if(num(&p, w, f, &w->r[i], err, nerr) < 0)
					return -1;
			break;
		case Knum:
			if(num(&p, w, f, numfield(w, f->bit), err, nerr) < 0)
				return -1;
			break;
		case Kword:
			w->cd = ctl_word(&p);
			if(w->cd == nil)
				return badarg(f->name, err, nerr);
			break;
		}
	}
	return 0;
}

/* rio set(): a signed value is relative to cur, a bare one is abs */
static int
setv(Wctl *w, int bit, int v, int cur, int abs)
{
	return (w->rel & bit) ? cur + v : abs;
}

/* rio shift(): move [*minp, *maxp) inside [min, max] keeping its size */
static void
shift(int *minp, int *maxp, int min, int max)
{
	if(*maxp > max){
		*minp += max - *maxp;
		*maxp = max;
	}
	if(*minp < min){
		*maxp += min - *minp;
		if(*maxp > max)
			*maxp = max;
		*minp = min;
	}
}

void
wctl_rect(Wctl *w, LRect base, LRect scr, LRect *out)
{
	LRect r;

	r = base;
	if(w->set & Pr){
		r.x0 = w->r[0];
		r.y0 = w->r[1];
		r.x1 = w->r[2];
		r.y1 = w->r[3];
	}
	if(w->set & Pminx)
		r.x0 = setv(w, Pminx, w->minx, r.x0, w->minx);
	if(w->set & Pminy)
		r.y0 = setv(w, Pminy, w->miny, r.y0, w->miny);
	if(w->set & Pmaxx)
		r.x1 = setv(w, Pmaxx, w->maxx, r.x1, w->maxx);
	if(w->set & Pmaxy)
		r.y1 = setv(w, Pmaxy, w->maxy, r.y1, w->maxy);
	if(w->set & Pdx)
		r.x1 = setv(w, Pdx, w->dx, r.x1, r.x0 + w->dx);
	if(w->set & Pdy)
		r.y1 = setv(w, Pdy, w->dy, r.y1, r.y0 + w->dy);
	if(w->cmd == Wmove){
		r.x1 = r.x0 + (base.x1 - base.x0);
		r.y1 = r.y0 + (base.y1 - base.y0);
	}else{
		if(r.x1 < r.x0 + WMINW)
			r.x1 = r.x0 + WMINW;
		if(r.y1 < r.y0 + WMINH)
			r.y1 = r.y0 + WMINH;
	}
	if(r.x1 - r.x0 > scr.x1 - scr.x0)
		r.x1 = r.x0 + (scr.x1 - scr.x0);
	if(r.y1 - r.y0 > scr.y1 - scr.y0)
		r.y1 = r.y0 + (scr.y1 - scr.y0);
	shift(&r.x0, &r.x1, scr.x0, scr.x1);
	shift(&r.y0, &r.y1, scr.y0, scr.y1);
	*out = r;
}

/* rio newrect() without the cascade: the base of a new window's rect */
enum { NEWW = 600, NEWH = 400 };

int
wctl_newwidth(Wctl *w, LRect scr)
{
	LRect base, r;

	if((w->set & (Pr|Pdx|Pminx|Pmaxx)) == 0)
		return 0;
	base.x0 = scr.x0;
	base.y0 = scr.y0;
	base.x1 = scr.x0 + NEWW;
	base.y1 = scr.y0 + NEWH;
	wctl_rect(w, base, scr, &r);
	return r.x1 - r.x0;
}

int
wctl_attach(char *aname, Wctl *w, int *id, char *err, int nerr)
{
	if(aname[0] == '\0' || strcmp(aname, "none") == 0)
		return Anone;
	if(strcmp(aname, "ctl") == 0)
		return Actl;
	if(strncmp(aname, "new", 3) == 0 && (aname[3] == '\0' || aname[3] == ' ' || aname[3] == '\t')){
		if(wctl_parse(aname, w, err, nerr) < 0)
			return -1;
		if(w->rest != nil){
			lsnprint(err, nerr, "bad attach spec");
			return -1;
		}
		return Anew;
	}
	if(aname[0] < '0' || aname[0] > '9' || action_parsenum(aname, id) < 0 || *id <= 0){
		lsnprint(err, nerr, "unknown id in attach");
		return -1;
	}
	return Aid;
}

int
wctl_fmt(char *buf, int n, int x0, int y0, int x1, int y1, char *s1, char *s2)
{
	return lsnprint(buf, n, "%11d %11d %11d %11d %11s %11s ", x0, y0, x1, y1, s1, s2);
}
