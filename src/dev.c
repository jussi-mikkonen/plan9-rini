/*
 * dev.c: per-file semantics of a window (rio's /dev). Everything here runs
 * on the main thread. Blocking reads wait in w->pend and are answered by
 * dev_wakeup, dev_flush or retire (win.c): exactly one respond per Req.
 */
#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <mouse.h>
#include <cursor.h>
#include <fcall.h>
#include <9p.h>
#include "layout.h"
#include "desk.h"
#include "input.h"
#include "ev.h"
#include "wq.h"
#include "wctl.h"
#include "tbuf.h"
#include "dat.h"
#include "fns.h"

/*
 * Snarf: the parent's /dev/snarf when rini runs inside a window system,
 * reopened per put; else rini's own buffer.
 */
int snarfparent;
static char *snarfbuf;
static long nsnarfbuf;

void
snarf_init(void)
{
	snarfparent = access("/dev/snarf", AEXIST) == 0;
}

char*
snarf_get(long *n)
{
	char *s;
	int fd;

	if(!snarfparent){
		s = emalloc9p(nsnarfbuf + 1);
		memmove(s, snarfbuf, nsnarfbuf);
		*n = nsnarfbuf;
		return s;
	}
	s = emalloc9p(MAXSNARF + 1);
	*n = 0;
	if((fd = open("/dev/snarf", OREAD)) >= 0){
		if((*n = readn(fd, s, MAXSNARF)) < 0)
			*n = 0;
		close(fd);
	}
	return s;
}

void
snarf_put(char *s, long n)
{
	int fd;

	if(snarfparent){
		if((fd = open("/dev/snarf", OWRITE|OTRUNC)) >= 0){
			write(fd, s, n);
			close(fd);
		}
		return;
	}
	free(snarfbuf);
	snarfbuf = emalloc9p(n + 1);
	memmove(snarfbuf, s, n);
	nsnarfbuf = n;
}

/*
 * kbdtap (rio's semantics): one reader and one writer globally, whichever
 * window directory they opened, or a none attach. Keys that survived chord
 * matching queue for the reader and never go around it; the write-back is
 * delivered only while focus is still where the key was typed. tapr, tapw
 * are the opener fids.
 */
static Tapq tapq;
static Faux *tapr, *tapw;
static Req *tappend;	/* tapr's one parked read */
static int tapfocus;	/* the window the last read message was typed in */

/* answers the parked read with the next queued message; Etooshort when it does not fit */
static void
tapanswer(void)
{
	Req *r;
	int n;

	r = tappend;
	n = tap_read(&tapq, r->ofcall.data, r->ifcall.count);
	if(n == 0)
		return;
	tappend = nil;
	if(n < 0){
		respond(r, Etooshort);
		return;
	}
	tapfocus = tapq.from;
	r->ofcall.count = n;
	respond(r, nil);
}

/*
 * The reader is gone: what it never read goes to the window it was typed in,
 * if that still has focus; a window that lost focus already got its K.
 */
static void
tapclose(void)
{
	char m[KMSGLEN];
	Win *w;
	int n;

	tapr = nil;
	while((n = tap_read(&tapq, m, sizeof m)) > 0)
		if(m[0] != 'z' && tapq.from == desk_focused(&desk) && (w = win_lookid(tapq.from)) != nil && !w->deleted)
			kbd_deliver(w, m, n - 1);
}

int
dev_tap(int focusid, char *msg, int len)
{
	if(tapr == nil)
		return -1;
	tap_push(&tapq, focusid, msg, len);
	if(tappend != nil)
		tapanswer();
	return 0;
}

void
tap_retire(Win *w)
{
	Req *r;

	if(tapw != nil && tapw->w == w)
		tapw = nil;
	if(tapr == nil || tapr->w != w)
		return;
	r = tappend;
	tappend = nil;
	tapclose();
	if(r != nil)
		respond(r, Edeleted);
}

static char*
tapwrite(char *d, ulong n)
{
	static char err[ERRMAX];	/* respond keeps the pointer */
	char *m[KQ];
	int l[KQ], i, k;
	Win *w;

	k = tap_split(d, n, m, l, KQ, err, sizeof err);
	if(k < 0)
		return err;
	w = win_focused();
	if(w == nil || w->id != tapfocus)	/* focus moved since the key was typed: drop, as rio */
		return nil;
	for(i = 0; i < k; i++)
		kbd_deliver(w, m[i], l[i]);
	return nil;
}

/*
 * The srv proc answers Tclunk before main sees it; it posts the clunk
 * before reading the next message, so any clunk that precedes this
 * Topen is already queued. Apply them so a quick reopen is not "in use".
 */
static void
drainclunks(void)
{
	Faux *f;

	while((f = nbrecvp(clunkc)) != nil)
		fs_clunk(f);
}

void
dev_open(Req *r)
{
	Faux *a;
	Win *w;
	int m;

	drainclunks();
	a = r->fid->aux;
	w = a->w;
	m = r->ifcall.mode & 3;
	if(w != nil && w->deleted){
		respond(r, Edeleted);
		return;
	}
	if((m != OWRITE && !(fs_perm(a->file) & 0400)) || (m != OREAD && !(fs_perm(a->file) & 0200))){
		respond(r, Eperm);
		return;
	}
	switch(a->file){	/* w is nil in the ctl tree and on a none attach (screen, wctl, kbdtap) */
	case Qevent:
		ctlfs_evopen(a);
		break;
	case Qmouse:
		if(w->q.mouseopen){
			respond(r, Einuse);
			return;
		}
		wq_mouseopen(&w->q, 1);
		break;
	case Qkbd:
		if(w->q.kbdopen){
			respond(r, Einuse);
			return;
		}
		wq_kbdopen(&w->q, 1);
		break;
	case Qkbdtap:
		if((m != OWRITE && tapr != nil) || (m != OREAD && tapw != nil)){
			respond(r, Einuse);
			return;
		}
		if(m != OWRITE){
			tapr = a;
			tap_init(&tapq);
		}
		if(m != OREAD)
			tapw = a;
		break;
	case Qconsctl:
		if(w->consctlopen){
			respond(r, Einuse);
			return;
		}
		w->consctlopen = 1;
		break;
	case Qtext:
		if(r->ifcall.mode & OTRUNC)
			term_clear(w);
		break;
	case Qwctl:
		if(w == nil || m == OWRITE)	/* as rio: only a window's readers hold wctl */
			break;
		if(w->wctlopen){
			respond(r, Einuse);
			return;
		}
		w->wctlopen = 1;
		w->q.wctlready = 1;	/* the first read answers at once */
		break;
	}
	a->open = 1;
	a->mode = m;
	respond(r, nil);
}

void
dev_close(Faux *a)
{
	Win *w;

	w = a->w;
	switch(a->file){	/* w nil: a none attach, see dev_open */
	case Qmouse:
		wq_mouseopen(&w->q, 0);
		w->winnameread = 0;
		term_redraw(w);
		break;
	case Qkbd:
		wq_kbdopen(&w->q, 0);
		break;
	case Qkbdtap:	/* a parked read holds the fid: tappend is not this one's */
		if(a == tapr)
			tapclose();
		if(a == tapw)
			tapw = nil;
		break;
	case Qconsctl:
		w->consctlopen = 0;
		term_ctl(w, Creset);
		break;
	case Qwctl:
		if(w != nil && a->mode != OWRITE)
			w->wctlopen = 0;
		break;
	case Qsnarf:	/* the real snarf is replaced only now, as rio */
		if(a->mode != OREAD)
			snarf_put(a->sbuf, a->nsbuf);
		free(a->sbuf);
		a->sbuf = nil;
		break;
	}
}

/* the window's pixels as readimage wants them: 60-byte header, then rows, unloaded in one call */
static void
imageread(Req *r, Image *i)
{
	char hdr[64], ch[16];
	uchar *rows;
	uvlong off;
	long n, bpl, y, ny, skip;

	if(i == nil){
		respond(r, "window has no image");
		return;
	}
	if(r->ifcall.offset < 60){
		snprint(hdr, sizeof hdr, "%11s %11d %11d %11d %11d ", chantostr(ch, i->chan),
			i->r.min.x, i->r.min.y, i->r.max.x, i->r.max.y);
		readbuf(r, hdr, 60);
		respond(r, nil);
		return;
	}
	off = r->ifcall.offset - 60;
	bpl = bytesperline(i->r, i->depth);
	r->ofcall.count = 0;
	if(r->ifcall.count == 0 || off >= (uvlong)bpl * Dy(i->r)){
		respond(r, nil);
		return;
	}
	y = off / bpl;
	skip = off % bpl;
	ny = (skip + r->ifcall.count + bpl - 1) / bpl;
	if(ny > Dy(i->r) - y)
		ny = Dy(i->r) - y;
	rows = emalloc9p(ny * bpl);
	if(unloadimage(i, Rect(i->r.min.x, i->r.min.y + y, i->r.max.x, i->r.min.y + y + ny), rows, ny * bpl) < 0){
		free(rows);
		respond(r, "cannot read image");
		return;
	}
	n = ny * bpl - skip;
	if(n > r->ifcall.count)
		n = r->ifcall.count;
	memmove(r->ofcall.data, rows + skip, n);
	free(rows);
	r->ofcall.count = n;
	respond(r, nil);
}

void
dev_read(Req *r)
{
	Faux *a;
	Win *w;
	char buf[WCTLLEN+1], *s;
	long n;

	a = r->fid->aux;
	if(a->ctl){
		ctlfs_read(r);
		return;
	}
	w = a->w;
	if(w != nil && w->deleted){
		respond(r, Edeleted);
		return;
	}
	switch(a->file){
	case Qtext:
		s = term_text(w, &n);
		readbuf(r, s, n);
		free(s);
		break;
	case Qwdir:
		readstr(r, w->wdir);
		break;
	case Qsnarf:
		s = snarf_get(&n);
		readbuf(r, s, n);
		free(s);
		break;
	case Qwindow:
	case Qscreen:
		imageread(r, a->file == Qwindow ? w->img : screen);
		return;
	case Qwinid:
		snprint(buf, sizeof buf, "%11d ", w->id);
		readstr(r, buf);
		break;
	case Qwinname:
		if(w->name[0] == 0){
			respond(r, "window has no name");
			return;
		}
		w->winnameread = 1;
		readstr(r, w->name);
		break;
	case Qlabel:
		readstr(r, w->label);
		break;
	case Qcursor:
		respond(r, "cursor read not implemented");
		return;
	case Qconsctl:
		respond(r, Eperm);
		return;
	case Qkbdtap:
		if(tappend != nil){
			respond(r, Ebusy);
			return;
		}
		tappend = r;
		tapanswer();
		return;
	default:	/* mouse, kbd, cons, wctl */
		if(w == nil){
			wctl_fmt(buf, sizeof buf, screen->r.min.x, screen->r.min.y, screen->r.max.x, screen->r.max.y, "nowindow", "nowindow");
			readbuf(r, buf, WCTLLEN);
			break;
		}
		if((a->file == Qwctl && r->ifcall.count < 48) || (a->file == Qmouse && r->ifcall.count < MOUSELEN)){
			respond(r, Etooshort);
			return;
		}
		if(w->npend == Npend){
			respond(r, Ebusy);
			return;
		}
		w->pend[w->npend++] = r;
		dev_wakeup(w);
		return;
	}
	respond(r, nil);
}

static int
ready(Win *w, Req *r, int file)
{
	switch(file){
	case Qmouse:
		return wq_mouseready(&w->q);
	case Qkbd:
		return wq_kbdready(&w->q);
	case Qcons:
		return r->ifcall.type == Twrite ? term_canwrite(w) : term_ready(w);
	}
	return w->q.wctlready;
}

/* decodes with the fid's carry and answers r */
static void
conswrite(Win *w, Req *r)
{
	Faux *a;
	Rune *rp;
	long nr;

	a = r->fid->aux;
	rp = emalloc9p((r->ifcall.count + TUTFMAX) * sizeof(Rune));
	nr = tbuf_utf8(a->part, &a->npart, r->ifcall.data, r->ifcall.count, rp);
	if(nr > 0)
		term_output(w, rp, nr);
	free(rp);
	r->ofcall.count = r->ifcall.count;
	respond(r, nil);
}

void
dev_wakeup(Win *w)
{
	Req *r;
	char buf[WCTLLEN+1];
	int i, n, k, file;

	k = 0;
	for(i = 0; i < w->npend; i++){
		r = w->pend[i];
		file = ((Faux*)r->fid->aux)->file;
		if(file == Qtext)	/* only its parked writes get here: as cons */
			file = Qcons;
		if(!ready(w, r, file)){
			w->pend[k++] = r;
			continue;
		}
		switch(file){
		case Qmouse:	/* count >= MOUSELEN: checked in dev_read */
			n = wq_mouseread(&w->q, buf);
			memmove(r->ofcall.data, buf, n);
			break;
		case Qkbd:
			n = wq_kbdread(&w->q, r->ofcall.data, r->ifcall.count);
			if(n == 0){	/* head message does not fit; 0 would read as EOF */
				respond(r, Etooshort);
				continue;
			}
			break;
		case Qcons:
			if(r->ifcall.type == Twrite){
				conswrite(w, r);
				continue;
			}
			n = term_read(w, r->ofcall.data, r->ifcall.count);
			break;
		default:
			wctl_fmt(buf, sizeof buf, w->sr.min.x, w->sr.min.y, w->sr.max.x, w->sr.max.y,
				w == win_focused() ? "current" : "notcurrent",
				desk_ishidden(&desk, w->id) ? "hidden" : "visible");
			n = WCTLLEN;
			if(n > r->ifcall.count)
				n = r->ifcall.count;
			memmove(r->ofcall.data, buf, n);
			w->q.wctlready = 0;
			break;
		}
		r->ofcall.count = n;
		respond(r, nil);
	}
	w->npend = k;
}

void
dev_flush(Req *r)
{
	Req *o;
	Faux *a;
	Win *w;
	int i;

	o = r->oldreq;
	/* only reads and cons writes are parked; a Tflush has no fid, a failed Tattach no aux */
	if(o->ifcall.type == Tread || o->ifcall.type == Twrite){
		a = o->fid->aux;
		if(a->evpend == o){
			a->evpend = nil;
			respond(o, Einterrupted);
		}else if(tappend == o){
			tappend = nil;
			respond(o, Einterrupted);
		}else if((w = a->w) != nil)
			for(i = 0; i < w->npend; i++)
				if(w->pend[i] == o){
					memmove(w->pend + i, w->pend + i + 1, (--w->npend - i) * sizeof w->pend[0]);
					respond(o, Einterrupted);
					break;
				}
	}
	respond(r, nil);
}

static int
isctl(char *d, ulong n, char *s)
{
	return n >= strlen(s) && strncmp(d, s, strlen(s)) == 0;
}

LRect
lrect(Rectangle r)
{
	LRect l;

	l.x0 = r.min.x;
	l.y0 = r.min.y;
	l.x1 = r.max.x;
	l.y1 = r.max.y;
	return l;
}

Rectangle
rectof(LRect l)
{
	return Rect(l.x0, l.y0, l.x1, l.y1);
}

Win*
dev_newwin(Wctl *wc)
{
	Win *w;
	int px;

	px = wctl_newwidth(wc, lrect(screen->r));
	w = win_new(px > 0 ? strip_pxtomil(desk_cur(&desk), px) : DEFWIDTH);
	if(w != nil && (wc->set & Phide))
		desk_hide(&desk, w->id);
	return w;
}

char*
dev_spawn(Wctl *wc)
{
	Win *w;

	if((w = dev_newwin(wc)) == nil)
		return Ewindow;
	spawn(w->id, wc->rest != nil ? wc->rest : spawncmd, wc->set & Pcd ? wc->cd : nil);
	return nil;
}

/* float rects are viewport-relative, wctl_rect works in screen coordinates */
static void
floatwctl(Win *w, Wctl *wc)
{
	LRect b, o;

	desk_floatrect(&desk, w->id, &b);
	wctl_rect(wc, lrect(rectaddpt(rectof(b), screen->r.min)), lrect(screen->r), &o);
	desk_floatset(&desk, w->id, lrect(rectsubpt(rectof(o), screen->r.min)));
}

/* tiles: the x side sets the column width, the y side the stack share (no-op alone in a column) */
static void
tilewctl(Win *w, Wctl *wc)
{
	Strip *s;
	LRect o;

	s = desk_stripof(&desk, w->id);
	wctl_rect(wc, lrect(w->sr), lrect(screen->r), &o);
	if(wc->set & (Pr|Pdx|Pminx|Pmaxx))
		strip_setwidthid(s, w->id, strip_pxtomil(s, o.x1 - o.x0));
	if(wc->set & (Pr|Pdy|Pminy|Pmaxy))
		strip_setheightpx(s, w->id, o.y1 - o.y0);
}

static char*
wctlwrite(Win *w, char *data, ulong n)
{
	Wctl wc;
	static char err[ERRMAX];	/* returned to the caller */
	char buf[512];
	int hid, fl;

	if(n >= sizeof buf)
		return "wctl message too long";
	memmove(buf, data, n);
	buf[n] = 0;
	if(wctl_parse(buf, &wc, err, sizeof err) < 0)
		return err;
	if(wc.cmd == Wnew)
		return dev_spawn(&wc);
	if(wc.set & Pid){
		w = win_lookid(wc.id);
		if(w == nil || w->deleted)
			return Eunkid;
	}
	if(w == nil)
		return "no window";
	hid = desk_ishidden(&desk, w->id);
	fl = desk_isfloat(&desk, w->id);
	switch(wc.cmd){
	case Wresize:
	case Wmove:
		if(hid)
			break;
		if(fl)
			floatwctl(w, &wc);
		else if(wc.cmd == Wresize)
			tilewctl(w, &wc);
		break;
	case Wtop:
		if(hid)
			break;
		if(!fl || w->id == desk_focused(&desk))
			desk_focus(&desk, w->id);
		if(fl)
			desk_raise(&desk, w->id);
		break;
	case Wcurrent:
		if(!hid)
			desk_focus(&desk, w->id);
		break;
	case Wbottom:
		if(fl)
			desk_lower(&desk, w->id);
		break;
	case Whide:
		desk_hide(&desk, w->id);
		break;
	case Wunhide:
		desk_unhide(&desk, w->id);
		break;
	case Wscroll:
		term_setscroll(w, 1);
		break;
	case Wnoscroll:
		term_setscroll(w, 0);
		break;
	case Wset:
		if(wc.set & Ppid)
			win_setpid(w, wc.pid, 1);
		break;
	case Wdelete:
		win_delete(w);
		break;
	}
	return nil;
}

void
dev_write(Req *r)
{
	Faux *a;
	Win *w;
	char *d, *e, *s, *t;
	ulong n;
	int x, y;

	a = r->fid->aux;
	if(a->ctl){
		ctlfs_write(r);
		return;
	}
	w = a->w;
	d = r->ifcall.data;
	n = r->ifcall.count;
	e = nil;
	if(w != nil && w->deleted){
		respond(r, Edeleted);
		return;
	}
	switch(a->file){
	case Qcons:
	case Qtext:	/* the same path as cons */
		dprint("cons %d: %lud bytes\n", w->id, n);
		if(!term_canwrite(w)){
			if(w->npend == Npend){
				respond(r, Ebusy);
				return;
			}
			w->pend[w->npend++] = r;
			return;
		}
		conswrite(w, r);
		return;
	case Qconsctl:
		if(isctl(d, n, "rawon"))
			term_ctl(w, Crawon);
		else if(isctl(d, n, "rawoff"))
			term_ctl(w, Crawoff);
		else if(isctl(d, n, "holdon"))
			term_ctl(w, Choldon);
		else if(isctl(d, n, "holdoff"))
			term_ctl(w, Choldoff);
		else
			e = "unknown control message";
		break;
	case Qcursor:
		if(n < 72)
			w->hascursor = 0;
		else{
			w->cursor.offset = Pt(BGLONG((uchar*)d), BGLONG((uchar*)d + 4));
			memmove(w->cursor.clr, d + 8, 64);
			w->hascursor = 1;
		}
		if(w == win_focused())
			setcursor(mc, w->hascursor ? &w->cursor : nil);
		break;
	case Qlabel:
		free(w->label);
		w->label = emalloc9p(n + 1);
		memmove(w->label, d, n);
		w->label[n] = 0;
		if(!w->labeled){
			w->labeled = 1;
			rules_apply(w, w->label);
		}
		break;
	case Qmouse:
		if(wq_warp(&w->q, d, n, &x, &y) < 0)
			e = Ebadmouse;
		else if(w == win_focused())
			moveto(mc, Pt(x, y));
		break;
	case Qkbdtap:
		e = tapwrite(d, n);
		break;
	case Qwctl:
		e = wctlwrite(w, d, n);
		break;
	case Qwdir:	/* as rio */
		x = n;
		if(x > 0 && d[x-1] == '\n')
			x--;
		if(x == 0)
			break;
		/* d is not NUL-terminated, and %.*s counts runes, not bytes */
		s = emalloc9p(x + 1);
		memmove(s, d, x);
		s[x] = '\0';
		if(s[0] != '/'){
			t = smprint("%s/%s", w->wdir, s);
			free(s);
			s = t;
		}
		free(w->wdir);
		w->wdir = cleanname(s);
		break;
	case Qsnarf:
		if(a->nsbuf + n > MAXSNARF){
			e = "snarf too large";
			break;
		}
		a->sbuf = erealloc9p(a->sbuf, a->nsbuf + n);
		memmove(a->sbuf + a->nsbuf, d, n);
		a->nsbuf += n;
		break;
	default:
		e = Eperm;
	}
	if(e == nil)
		r->ofcall.count = n;
	respond(r, e);
}
