/*
 * term.c: the libframe shell around tbuf: per-window text state, drawing, the
 * scroll bar and the key/output/read/consctl entry points. Everything runs on
 * the one owner thread and every draw is gated by term_visible, so a graphics
 * client that owns the image is never painted over.
 */
#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <mouse.h>
#include <plumb.h>
#include <complete.h>
#include <cursor.h>
#include <frame.h>
#include <fcall.h>
#include <9p.h>
#include "layout.h"
#include "desk.h"
#include "input.h"
#include "wq.h"
#include "wctl.h"
#include "tbuf.h"
#include "tkeys.h"
#include "dat.h"
#include "fns.h"

enum {
	Scrollwid = 12,
	Scrollgap = 4,
	HiWater = 640000,
	LoWater = 400000
};

struct Term {
	Tbuf tb;
	Frame f;
	int finit;
	Rectangle textr, scrollr;
	long org;	/* first rune shown */
	int scrolling;
	int stale;	/* the frame no longer matches org and the buffer */
	int mstate;
	long anchor;	/* fixed end of a drag selection */
	int lastb;
	ulong lastclick;
	long lastq;	/* where the last plain click landed, -1 if it selected a range */
	int sbut;	/* scroll bar button held, as a mask */
	int my;	/* last mouse y, image coordinates, clamped to the bar */
};

enum {
	Midle,
	Mselect,
	Mscroll,

	Cut = 0,	/* menu2str order */
	Paste,
	Snarf,
	Plumb,
	Look,
	Send,
	Scroll,

	Plumbmax = 8192 - 1024,	/* a plumb message must fit one 9P message */

	Dclick = 500,	/* ms */
	Tfirst = 200,	/* auto-repeat delays, ms */
	Trepeat = 100
};

Tkeytab tk;
Channel *tickc;
static ulong tickgen;
static int tickwin, tickdir;
static Image *cols[NCOL];
static Image *holdcol;
static int plumbfd = -1;
static char *menu2str[] = { "cut", "paste", "snarf", "plumb", "look", "send", "scroll", nil };
static Menu menu2 = { menu2str };

int
term_init(void)
{
	cols[BACK] = display->white;
	cols[HIGH] = solid(0xCCCCCCFF);
	cols[BORD] = solid(0x999999FF);
	cols[TEXT] = cols[HTEXT] = display->black;
	holdcol = solid(DMedblue);
	if(cols[HIGH] == nil || cols[BORD] == nil || holdcol == nil){
		fprint(2, "term_init: allocimage: %r\n");
		return -1;
	}
	tickc = chancreate(sizeof(ulong), 0);
	if(tickc == nil){
		fprint(2, "term_init: chancreate: %r\n");
		return -1;
	}
	plumbfd = plumbopen("send", OWRITE|OCEXEC);	/* -1 without a plumber: plumb then does nothing */
	tkeys_init(&tk);
	tkeys_defaults(&tk);
	return 0;
}

Term*
term_new(void)
{
	Term *t;

	t = emalloc9p(sizeof *t);
	tbuf_init(&t->tb, HiWater, LoWater);
	t->stale = 1;
	t->lastq = -1;
	return t;
}

void
term_free(Term *t)
{
	if(t->finit)
		frclear(&t->f, 1);
	tbuf_free(&t->tb);
	free(t);
}

/* text is drawn unless a client reads the image through the mouse */
int
term_visible(Win *w)
{
	return w->img != nil && w->term->finit && !(w->q.mouseopen && w->winnameread);
}

static void
textcols(Term *t)
{
	t->f.cols[TEXT] = t->f.cols[HTEXT] = t->tb.holding ? holdcol : cols[TEXT];
}

/* rio wfill: insert in bounded chunks until the frame is full or the text ends */
static void
fill(Term *t)
{
	Rune *rp;
	long n, i, nl, m;

	while(!t->f.lastlinefull){
		n = t->tb.nr - (t->org + t->f.nchars);
		if(n == 0)
			break;
		if(n > 2000)
			n = 2000;
		rp = t->tb.r + t->org + t->f.nchars;
		/* frinsert of more than fits is expensive: stop at the lines still free */
		nl = t->f.maxlines - t->f.nlines;
		m = 0;
		for(i = 0; i < n;)
			if(rp[i++] == '\n' && ++m >= nl)
				break;
		frinsert(&t->f, rp, rp + i, t->f.nchars);
	}
}

/* rio wscrdraw shape: bar BORD, thumb BACK with a BORD right edge */
static void
scrdraw(Win *w)
{
	Term *t;
	Rectangle r, th;
	long tot, p0, p1;
	int h;

	t = w->term;
	r = t->scrollr;
	th = r;
	h = Dy(r);
	tot = t->tb.nr;
	p0 = t->org;
	p1 = t->org + t->f.nchars;
	if(tot > 0){	/* vlong: h * a full buffer overflows a 32-bit long */
		th.min.y += (vlong)h*p0/tot;
		th.max.y -= (vlong)h*(tot-p1)/tot;
	}
	if(th.max.y < th.min.y+2){
		if(th.min.y+2 <= r.max.y)
			th.max.y = th.min.y+2;
		else
			th.min.y = th.max.y-2;
	}
	draw(w->img, r, cols[BORD], nil, ZP);
	draw(w->img, th, cols[BACK], nil, ZP);
	th.min.x = th.max.x-1;
	draw(w->img, th, cols[BORD], nil, ZP);
}

static long
clampl(long v, long lo, long hi)
{
	if(v < lo)
		return lo;
	if(v > hi)
		return hi;
	return v;
}

/* rio wsetselect: the frame selection follows q0..q1 */
static void
setsel(Term *t)
{
	Frame *f;
	long p0, p1;

	f = &t->f;
	p0 = clampl(t->tb.q0 - t->org, 0, f->nchars);
	p1 = clampl(t->tb.q1 - t->org, 0, f->nchars);
	if(p0 == f->p0 && p1 == f->p1)
		return;
	frdrawsel(f, frptofchar(f, f->p0), f->p0, f->p1, 0);
	frdrawsel(f, frptofchar(f, p0), p0, p1, 1);
	f->p0 = p0;
	f->p1 = p1;
}

static void
redraw(Win *w)
{
	Term *t;

	t = w->term;
	frdelete(&t->f, 0, t->f.nchars);
	draw(w->img, insetrect(w->img->r, Border), cols[BACK], nil, ZP);
	textcols(t);
	fill(t);
	setsel(t);
	scrdraw(w);
	t->stale = 0;
}

/* bring the frame up to date with the buffer; nothing is drawn in graphics mode */
static void
sync(Win *w)
{
	Term *t;
	Tbuf *b;

	t = w->term;
	b = &t->tb;
	if(!term_visible(w)){
		t->stale = 1;
		return;
	}
	t->org -= b->trimmed;
	if(t->org < 0){
		t->org = 0;
		t->stale = 1;
	}
	if(t->org > b->nr)
		t->org = b->nr;
	if(t->stale || b->chg < t->org)
		redraw(w);
	else if(b->chg >= 0 && b->chg <= t->org + t->f.nchars){
		frdelete(&t->f, b->chg - t->org, t->f.nchars);
		fill(t);
	}
	setsel(t);
	scrdraw(w);
	tbuf_clean(b);
	flushimage(display, 1);
}

static void
setorigin(Term *t, long org)
{
	t->org = org;
	t->stale = 1;
}

static void
scrolllines(Win *w, int n)
{
	Term *t;

	t = w->term;
	if(!term_visible(w))
		return;
	if(n < 0)
		setorigin(t, tbuf_backnl(&t->tb, t->org, -n));
	else
		setorigin(t, t->org + frcharofpt(&t->f, Pt(t->textr.min.x, t->textr.min.y + n*t->f.font->height)));
}

/* rio wshow: make q visible, near the middle-top of a fresh view; leaves the frame synced */
static void
show(Win *w, long q)
{
	Term *t;
	long qe, o;

	t = w->term;
	if(!term_visible(w))
		return;
	qe = t->org + t->f.nchars;
	if(t->org <= q && (q < qe || (q == qe && qe == t->tb.nr)))
		return;
	o = tbuf_backnl(&t->tb, q, 4*t->f.maxlines/5);
	if(!(q > t->org && o < t->org))	/* long lines: never go back when going forwards */
		setorigin(t, o);
	sync(w);
	while(q > t->org + t->f.nchars && t->f.nchars > 0){	/* backnl counts at most 128 runes a line; an empty frame cannot advance */
		scrolllines(w, 1);
		sync(w);
	}
}

void
term_reshape(Win *w)
{
	Term *t;

	t = w->term;
	w->winnameread = 0;	/* a client must read winname again */
	t->textr = insetrect(w->img->r, Border);
	t->scrollr = t->textr;
	t->scrollr.max.x = t->scrollr.min.x + Scrollwid;
	t->textr.min.x += Scrollwid + Scrollgap;
	if(t->finit)
		frclear(&t->f, 0);
	frinit(&t->f, t->textr, font, w->img, cols);
	t->finit = 1;
	textcols(t);
	t->stale = 1;
	if(!w->q.mouseopen)	/* an open mouse means the client owns the new image */
		sync(w);
}

/* back from graphics: repaint, and bring the cursor into view */
void
term_redraw(Win *w)
{
	if(!term_visible(w))
		return;
	w->term->stale = 1;
	sync(w);
	show(w, w->term->tb.q0);
}

/* rio wsnarf; tbuf holds no NUL runes, so %S converts the whole range */
static void
snarfsel(Tbuf *b)
{
	char *s;

	if(b->q0 == b->q1)
		return;
	s = smprint("%.*S", (int)(b->q1 - b->q0), b->r + b->q0);
	snarf_put(s, strlen(s));
	free(s);
}

/*
 * File name completion: the word before the cursor, directory part relative to wdir.
 * libcomplete reads the directory on this thread; accepted for local dirs.
 */
static void
completename(Win *w)
{
	Tbuf *b;
	Completion *c;
	Fmt f;
	Rune *rp;
	char *dir, *str;
	long ps, es, nr, ql;
	int i, athq;

	b = &w->term->tb;
	tbuf_delete(b, b->q0, b->q1);
	if(b->q0 < b->nr && b->r[b->q0] > ' ')	/* must be at the end of a word */
		return;
	ps = tbuf_wordstart(b, b->q0);
	for(es = b->q0; es > ps && b->r[es-1] != '/'; es--)
		;
	if(es > ps && b->r[ps] == '/')
		dir = smprint("%.*S", (int)(es - ps), b->r + ps);
	else
		dir = smprint("%s/%.*S", *w->wdir ? w->wdir : ".", (int)(es - ps), b->r + ps);
	str = smprint("%.*S", (int)(b->q0 - es), b->r + es);
	c = complete(cleanname(dir), str);
	free(dir);
	free(str);
	if(c == nil)
		return;
	if(c->advance){
		rp = runesmprint("%s", c->string);
		tbuf_insert(b, b->q0, rp, runestrlen(rp));
		free(rp);
	}else{
		/* showcandidates: a listing line above the current input line */
		runefmtstrinit(&f);
		fmtprint(&f, "%s", c->nmatch == 0 ? "[no matches in " : "[");
		if(c->nfile > 32)
			fmtprint(&f, "%d files]\n", c->nfile);
		else{
			for(i = 0; i < c->nfile; i++)
				fmtprint(&f, i > 0 ? " %s" : "%s", c->filename[i]);
			fmtprint(&f, "]\n");
		}
		rp = runefmtstrflush(&f);
		nr = runestrlen(rp);
		ql = b->qh < b->q0 ? b->qh : b->q0;
		ql = tbuf_bol(b, ql);
		athq = ql == b->qh;
		tbuf_insert(b, ql, rp, nr);
		if(athq)	/* the output point moves past a listing placed at it */
			b->qh += nr;
		free(rp);
	}
	freecompletion(c);
}

void
term_key(Win *w, Rune r, int shift)
{
	Term *t;
	Tbuf *b;
	int act, fl, n;

	t = w->term;
	b = &t->tb;
	act = tkeys_lookup(&tk, shift, r);
	/* raw: the view keys still scroll unless the mouse is open, as rio's navigation block */
	if(b->rawing && (w->q.mouseopen || (b->q0 == b->nr && (act < Tlineup || act > Ttop)))){
		tbuf_rawkey(b, &r, 1);
		return;
	}
	switch(act){
	case Tlineup:
	case Tlinedown:
		scrolllines(w, act == Tlineup ? -1 : 1);
		break;
	case Tpageup:
	case Tpagedown:
		/* two thirds of the window, as rio */
		n = 2*t->f.maxlines/3;
		scrolllines(w, act == Tpageup ? -n : n);
		break;
	case Ttop:
		setorigin(t, 0);
		break;
	default:
		switch(act){	/* typing replaces the selection and snarfs it (rio wkeyctl) */
		case Tinsert:
		case Ttab:
		case Treturn:
		case Tnewline:
		case Tbackspace:
		case Tkillword:
		case Tkillline:
		case Tcomplete:
			snarfsel(b);
		}
		fl = tbuf_key(b, act, r, tk.c64);
		if(fl & TFintr)
			win_note(w, "interrupt");
		if(fl & TFhold){
			textcols(t);
			t->stale = 1;
		}
		if(fl & TFcomplete){
			completename(w);
			fl |= TFshow;
		}
		sync(w);
		if(fl & TFshow)
			show(w, b->q0);
	}
	sync(w);
}

void
term_output(Win *w, Rune *s, long n)
{
	Term *t;

	t = w->term;
	tbuf_output(&t->tb, s, n);
	sync(w);
	if(t->scrolling || w->q.mouseopen)
		show(w, t->tb.qh);
}

/* rio's Truncate: an OTRUNC open of text empties the buffer */
void
term_clear(Win *w)
{
	tbuf_delete(&w->term->tb, 0, w->term->tb.nr);
	sync(w);
}

/* flow control: output blocks while the output point is below the view */
int
term_canwrite(Win *w)
{
	Term *t;

	t = w->term;
	return t->scrolling || w->q.mouseopen || !term_visible(w)
		|| t->tb.qh <= t->org + t->f.nchars;
}

int
term_ready(Win *w)
{
	return tbuf_ready(&w->term->tb);
}

int
term_read(Win *w, char *buf, int n)
{
	return tbuf_read(&w->term->tb, buf, n);
}

char*
term_text(Win *w, long *nb)
{
	char *s;

	s = smprint("%.*S", (int)w->term->tb.nr, w->term->tb.r);
	*nb = strlen(s);
	return s;
}

void
term_ctl(Win *w, int c)
{
	Term *t;

	t = w->term;
	if(tbuf_ctl(&t->tb, c) & TFhold){
		textcols(t);
		t->stale = 1;
	}
	sync(w);
}

/*
 * Mouse. rio's selection and scroll loops read the mouse channel themselves
 * which would stop 9P service; here every event advances a state
 * machine and a short-lived ticker proc supplies the auto-repeat.
 */
static void
tickproc(void *arg)
{
	ulong g;

	threadsetname("tick");
	g = (ulong)(uintptr)arg;
	sleep(Tfirst);
	while(g == tickgen){
		sendul(tickc, g);
		sleep(Trepeat);
	}
}

static void
starttick(Win *w, int dir)
{
	if(tickwin == w->id && tickdir == dir)
		return;
	tickgen++;
	tickwin = w->id;
	tickdir = dir;
	proccreate(tickproc, (void*)(uintptr)tickgen, 4096);
}

static void
stoptick(void)
{
	tickgen++;
	tickwin = 0;
}

/* back or forward by the pointer's height in the bar, at least a line */
static void
scrollstep(Win *w, int back, int y)
{
	Term *t;
	int n;

	t = w->term;
	n = (y - t->scrollr.min.y) / t->f.font->height;
	if(n < 1)
		n = 1;
	scrolllines(w, back ? -n : n);
}

static void
extendsel(Term *t, long q)
{
	t->tb.q0 = q < t->anchor ? q : t->anchor;
	t->tb.q1 = q > t->anchor ? q : t->anchor;
}

/* rio wplumb: the selection, or the blank-delimited word around the cursor with a click attribute */
static void
plumbsel(Win *w)
{
	Tbuf *b;
	Plumbmsg *m;
	char buf[32];
	long p0, p1;

	if(plumbfd < 0)
		return;
	b = &w->term->tb;
	m = emalloc9p(sizeof *m);
	m->src = estrdup9p("rio");
	m->wdir = estrdup9p(w->wdir);
	m->type = estrdup9p("text");
	p0 = b->q0;
	p1 = b->q1;
	if(p1 <= p0){
		while(p0 > 0 && b->r[p0-1] != ' ' && b->r[p0-1] != '\t' && b->r[p0-1] != '\n')
			p0--;
		while(p1 < b->nr && b->r[p1] != ' ' && b->r[p1] != '\t' && b->r[p1] != '\n')
			p1++;
		snprint(buf, sizeof buf, "click=%ld", b->q0 - p0);
		m->attr = plumbunpackattr(buf);
	}
	if(p1 - p0 <= Plumbmax){
		m->data = smprint("%.*S", (int)(p1 - p0), b->r + p0);
		m->ndata = strlen(m->data);
		plumbsend(plumbfd, m);
	}
	plumbfree(m);
}

/* rio wlook: the next occurrence of the selection, wrapping */
static void
looksel(Win *w)
{
	Tbuf *b;
	long i, n, e;

	b = &w->term->tb;
	i = b->q1;
	n = i - b->q0;
	e = b->nr - n;
	if(n <= 0 || e < n)
		return;
	if(i > e)
		i = 0;
	while(runestrncmp(b->r + b->q0, b->r + i, n) != 0)
		i = i < e ? i + 1 : 0;
	b->q0 = i;
	b->q1 = i + n;
	show(w, i);
}

void
term_setscroll(Win *w, int on)
{
	Term *t;

	t = w->term;
	t->scrolling = on;
	if(on){
		sync(w);
		show(w, t->tb.nr);
	}
}

/* rio button2menu; menuhit blocks 9P service while the button is held */
static void
button2menu(Win *w)
{
	Term *t;
	Tbuf *b;
	Rune *rp;
	char *s;
	long n;
	int fl;

	t = w->term;
	b = &t->tb;
	menu2str[Scroll] = t->scrolling ? "noscroll" : "scroll";
	switch(menuhit(2, mc, &menu2, wscreen)){
	case Cut:
		snarfsel(b);
		tbuf_delete(b, b->q0, b->q1);
		break;
	case Snarf:
		snarfsel(b);
		break;
	case Paste:
		s = snarf_get(&n);
		if(n > 0){
			rp = runesmprint("%.*s", (int)n, s);
			n = runestrlen(rp);
			tbuf_delete(b, b->q0, b->q1);
			if(b->rawing && b->q0 == b->nr)
				tbuf_rawkey(b, rp, n);
			else{
				tbuf_insert(b, b->q0, rp, n);
				b->q0 = b->q1 - n;	/* the pasted text is selected */
			}
			free(rp);
		}
		free(s);
		break;
	case Plumb:
		plumbsel(w);
		break;
	case Look:
		looksel(w);
		break;
	case Send:
		snarfsel(b);
		fl = tbuf_send(b);
		sync(w);
		if(fl & TFshow)
			show(w, b->q0);
		break;
	case Scroll:
		term_setscroll(w, !t->scrolling);
		break;
	}
	t->lastb = mc->buttons;	/* menuhit consumed the mouse until release */
	sync(w);
}

void
term_mouse(Win *w, Mouse *m)
{
	Term *t;
	Tbuf *b;
	Point p, c;
	int edge, y;
	long q, q0, q1;

	t = w->term;
	b = &t->tb;
	q0 = b->q0;
	q1 = b->q1;
	p = subpt(m->xy, Pt(w->q.dx, w->q.dy));
	edge = m->buttons & ~t->lastb;
	y = t->my = clampl(p.y, t->scrollr.min.y, t->scrollr.max.y);

	/* a wheel click is one scroll step; Mouse carries no Shift state, so mousescrollsize is not used */
	if(edge & (8|16))
		scrollstep(w, edge & 8, y);

	if(t->mstate == Midle){
		if(edge & 1 && ptinrect(p, t->textr)){
			q = t->org + frcharofpt(&t->f, p);
			if(m->msec - t->lastclick < Dclick && q == t->lastq){
				tbuf_dclick(b, q, &b->q0, &b->q1);
				t->lastq = -1;
			}else{
				b->q0 = b->q1 = t->anchor = q;
				t->mstate = Mselect;
			}
			b->goal = -1;
		}else if(edge & 2 && ptinrect(p, t->textr)){
			button2menu(w);	/* sets lastb from the released mouse */
			return;
		}else if(edge & (1|2|4) && ptinrect(p, t->scrollr)){
			t->mstate = Mscroll;	/* button 2 jumps below, as when held */
			t->sbut = edge & 1 ? 1 : edge & 2 ? 2 : 4;
			if(t->sbut != 2){
				scrollstep(w, t->sbut == 1, y);
				starttick(w, 0);
			}
		}
	}else if(t->mstate == Mselect){
		if(!(m->buttons & 1)){
			t->mstate = Midle;
			stoptick();
			t->lastclick = m->msec;
			t->lastq = b->q0 == b->q1 ? b->q0 : -1;
		}else{
			if(p.y < t->textr.min.y || p.y >= t->textr.max.y)
				starttick(w, p.y < t->textr.min.y ? -1 : 1);
			else
				stoptick();
			c.x = clampl(p.x, t->textr.min.x, t->textr.max.x-1);
			c.y = clampl(p.y, t->textr.min.y, t->textr.max.y-1);
			extendsel(t, t->org + frcharofpt(&t->f, c));
		}
	}
	if(t->mstate == Mscroll){
		if(!(m->buttons & t->sbut)){
			t->mstate = Midle;
			stoptick();
		}else if(t->sbut == 2)
			setorigin(t, tbuf_bol(b, (vlong)b->nr * (y - t->scrollr.min.y) / Dy(t->scrollr)));
	}
	t->lastb = m->buttons;
	/* plain motion draws nothing, as rio: a flush per event flickers a software cursor */
	if(t->stale || b->q0 != q0 || b->q1 != q1)
		sync(w);
}

void
term_tick(ulong gen)
{
	Win *w;
	Term *t;

	w = win_lookid(tickwin);
	if(gen != tickgen)
		return;
	if(w == nil || w->deleted){	/* the release will never come */
		stoptick();
		return;
	}
	if(!term_visible(w))
		return;
	t = w->term;
	if(t->mstate == Mscroll){
		scrollstep(w, t->sbut == 1, t->my);
		sync(w);
	}else if(t->mstate == Mselect){
		scrolllines(w, tickdir);
		sync(w);
		extendsel(t, t->org + (tickdir < 0 ? 0 : t->f.nchars));
		sync(w);
	}
}
