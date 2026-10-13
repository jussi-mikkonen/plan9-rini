/*
 * rini.c: main loop. Runs inside a rio window, reads /dev/kbd in a named
 * proc, routes chords through input.c and bind.c to action strings, routes
 * mouse and keys to client windows, serves them over 9P and renders via
 * render.c. relayout() after each event is the single commit point.
 *
 * Procs and threads: main (threadmain), kbdproc (this file), the libdraw
 * mouse proc, the lib9p srv proc, "spawn" per launch and "hangup" per delete.
 */
#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <mouse.h>
#include <keyboard.h>
#include <fcall.h>
#include <9p.h>
#include <cursor.h>
#include "layout.h"
#include "desk.h"
#include "action.h"
#include "bind.h"
#include "ctl.h"
#include "input.h"
#include "wq.h"
#include "wctl.h"
#include "dat.h"
#include "fns.h"

int debug;
Screen *wscreen;
Image *background;
Desk desk;
Bindtab bt;
Input in;
Mousectl *mc;
Channel *kbdc;

static Place places[MAXWIN];
static int nplaces;
static Mroute mr;
static Mpol mp;
static Image *ovl;	/* the hotkey overlay while it is up */
static Image *note;	/* the quit confirmation while it is up */
static ulong confirmgen;	/* the timer proc that may cancel the current note */
static Channel *confirmc;
static int startfd = -1;	/* notepg of the startup script's group */

static void
usage(void)
{
	fprint(2, "usage: rini [-D] [-m modspec] [-c never|always|on-overflow] [-s cmd] [-i file]  (default: rc -i in a text window)\n");
	fprint(2, "  modspec: Mod4 (default), %s (dev-only fallback), Mod4+Ctrl etc. (no Shift), or leader F12\n", FALLBACKMOD);
	threadexitsall("usage");
}

void
dprint(char *fmt, ...)
{
	va_list arg;

	if(!debug)
		return;
	va_start(arg, fmt);
	vfprint(2, fmt, arg);
	va_end(arg);
}

/* the single commit point for every focus change path */
void
relayout(void)
{
	static int lastfocus;
	Win *w, *o;
	int id;

	nplaces = desk_place(&desk, places);
	ctlfs_commit();
	render_sync(places, nplaces, 0);
	id = desk_focused(&desk);
	if(id != lastfocus){
		if((o = win_lookid(lastfocus)) != nil)
			wq_unfocus(&o->q);
		in.nfwd = 0;	/* the old window got its K: the next message gives the new one the whole held list */
		if((w = win_lookid(id)) != nil){
			w->q.wctlready = 1;
			setcursor(mc, w->hascursor ? &w->cursor : nil);
		}else
			setcursor(mc, nil);
		lastfocus = id;
	}
	for(w = wins; w != nil; w = w->next)
		dev_wakeup(w);
}

/*
 * The one Screen-replacing sequence, for an outer resize and a new background:
 * a Screen over the current background, every window reshaped onto it, then
 * the old Screen freed. -1 with the old state intact when allocscreen fails.
 */
static int
swapscreen(void)
{
	Screen *os, *ns;

	ns = allocscreen(screen, background, 0);
	if(ns == nil)
		return -1;
	os = wscreen;
	wscreen = ns;
	draw(screen, screen->r, background, nil, ZP);
	desk_resize(&desk, Dx(screen->r), Dy(screen->r));
	nplaces = desk_place(&desk, places);
	render_sync(places, nplaces, 1);
	freescreen(os);
	return 0;
}

static void
resized(void)
{
	Rectangle old;

	old = screen->r;
	if(getwindow(display, Refnone) < 0)
		sysfatal("getwindow: %r");
	dprint("resize: %R -> %R\n", old, screen->r);
	if(swapscreen() < 0)
		sysfatal("allocscreen: %r");
}

/* the old Screen goes before the old fill image it references */
int
rescreen(unsigned long rgba)
{
	Image *nb, *ob;

	nb = solid(rgba);
	if(nb == nil)
		return -1;
	ob = background;
	background = nb;	/* render.c fills new window images from it */
	if(swapscreen() < 0){
		background = ob;
		freeimage(nb);
		return -1;
	}
	bgcolor = rgba;
	freeimage(ob);
	return 0;
}

static void
kbdproc(void *v)
{
	Channel *c;
	char buf[256], *p;
	int fd, n;

	threadsetname("kbdproc");
	c = v;
	fd = open("/dev/kbd", OREAD|OCEXEC);	/* shared fd table: keep it out of clients */
	if(fd < 0){
		fprint(2, "kbdproc: open /dev/kbd: %r\n");
		threadexitsall("kbd");
	}
	for(;;){
		n = read(fd, buf, sizeof buf - 1);
		if(n <= 0)
			break;
		buf[n] = '\0';
		p = buf;
		while(p < buf + n){
			if(*p != '\0')
				sendp(c, strdup(p));
			p += strlen(p) + 1;
		}
	}
	fprint(2, "kbdproc: read /dev/kbd: %r\n");
	threadexitsall("kbd");
}

/* hangup to every note group, a second for sam to save, then kill; synchronous */
static void
signalall(char *msg)
{
	Win *w;

	for(w = wins; w != nil; w = w->next)
		if(w->notefd >= 0)
			write(w->notefd, msg, strlen(msg));
	if(startfd >= 0)
		write(startfd, msg, strlen(msg));
}

static void
exit_all(void)
{
	signalall("hangup");
	sleep(1000);
	signalall("kill");
	threadexitsall(nil);
}

static void
confirmproc(void *arg)
{
	threadsetname("confirm");
	sleep(2000);
	sendul(confirmc, (ulong)(uintptr)arg);
}

static void
notecancel(void)
{
	overlay_close(note);
	note = nil;
	confirmgen++;
}

/*
 * ko is about 2 KB: static, not on the small libthread main stack, because
 * relayout -> render_sync -> libdraw runs while it is live. Only the main
 * thread calls dokbd, so one instance is enough.
 */
static Kout ko;

static void
newwin(int wmil)
{
	Win *w;

	if((w = win_new(wmil)) != nil)
		spawn(w->id, spawncmd, nil);
	else
		dprint("strip full\n");
}

/* act's arguments when its first word is v, else nil */
static char*
verb(char *act, char *v)
{
	int n;

	n = strlen(v);
	if(strncmp(act, v, n) != 0 || (act[n] != '\0' && act[n] != ' ' && act[n] != '\t'))
		return nil;
	return act + n;
}

/*
 * The one dispatcher for keyboard chords and ctl lines. spawn takes rio's
 * `new` grammar after the word; fromkbd is for the quit confirmation.
 */
int
ui_action(char *act, int fromkbd, char *err, int nerr)
{
	Wctl wc;
	Win *w;
	char buf[CTLLINE+8], *args, *e;
	int quit;

	if(verb(act, "show-hotkey-overlay") != nil){
		if(ovl != nil){
			overlay_close(ovl);
			ovl = nil;
		}else
			ovl = overlay_bindings(&bt);
		return 0;
	}
	quit = verb(act, "quit") != nil;
	if(note != nil && fromkbd && !quit)
		notecancel();
	if(quit){
		if(!fromkbd || note != nil)	/* note is up for 2 s: confirmproc takes it down */
			exit_all();
		note = overlay_note("press Mod+Shift+e again to exit");
		confirmgen++;
		proccreate(confirmproc, (void*)(uintptr)confirmgen, 4096);
		return 0;
	}
	if((args = verb(act, "spawn")) != nil){
		snprint(buf, sizeof buf, "new %s", args);
		if(wctl_parse(buf, &wc, err, nerr) < 0)
			return -1;
		if((e = dev_spawn(&wc)) != nil){
			snprint(err, nerr, "%s", e);
			return -1;
		}
		return 0;
	}
	if(verb(act, "delete") != nil){
		if((w = win_focused()) != nil)
			win_delete(w);
		return 0;
	}
	return action_run(&desk, act, err, nerr);
}

void
kbd_deliver(Win *w, char *msg, int len)
{
	int r;

	if(w->q.kbdopen)
		wq_kbd(&w->q, msg, len);
	else if(msg[0] == 'c' && input_utf8dec(msg + 1, len - 1, &r) > 0)
		term_key(w, r, input_heldkey(&in, Kshift));
}

static void
dokbd(char *m)
{
	char err[128];
	Win *w;
	int i, c;

	if(input_kbd(&in, m, strlen(m), nsec()/1000000, &ko) < 0){
		dprint("kbd malformed: dropped\n");
		return;
	}
	dprint("kbd %s\n", m);
	c = ko.len > 0 && ko.msg[0] == 'c';
	if(ovl != nil){	/* a key or an action closes the overlay and goes no further; k and K still reach the client */
		if(c || ko.nact > 0){
			overlay_close(ovl);
			ovl = nil;
		}
		if(c)
			ko.len = 0;
		ko.nact = 0;
	}
	w = win_focused();
	if(ko.len > 0){
		dprint("fwd %.*s\n", ko.len, ko.msg);
		if(note != nil && c)
			notecancel();	/* the key is still delivered */
		/* chords were matched and swallowed in input_kbd: ktrans only sees what is left */
		if(w != nil && dev_tap(w->id, ko.msg, ko.len) < 0)
			kbd_deliver(w, ko.msg, ko.len);
	}
	for(i = 0; i < ko.nact; i++){
		dprint("action %s\n", ko.act[i]);
		if(ui_action(ko.act[i], 1, err, sizeof err) < 0)
			dprint("action error: %s\n", err);
	}
}

/* a blocking loop consumed the mouse up to a release: resync both routers */
static void
mreset(void)
{
	mroute_reset(&mr, mc->buttons);
	mpol_reset(&mp, mc->buttons);
}

/* the B3 menu; target is the window under the press, 0 for the background */
static void
domenu(int target)
{
	Rectangle r;
	Point p;
	LRect f;
	int i, j, id;

	i = mouse_menu3();
	id = target;
	if(i >= NMenu3)
		desk_unhide(&desk, desk.hid[i - NMenu3]);
	else if(i == MNew){
		if(mouse_sweep(&r) == 0)
			newwin(strip_pxtomil(desk_cur(&desk), Dx(r)));
	}else if(i >= 0){
		if(id == 0 && mouse_pointto(&p) == 0){
			j = place_hit(places, nplaces, p.x - screen->r.min.x, p.y - screen->r.min.y);
			id = j < 0 ? 0 : places[j].id;
		}
		if(id != 0)
			switch(i){
			case MResize:
			case MMove:
				if(!desk_isfloat(&desk, id)){
					desk_togglefloat(&desk, id);
					relayout();
				}
				if(desk_floatrect(&desk, id, &f)){
					r = rectaddpt(rectof(f), screen->r.min);
					/* a resize presses at the corner: it follows the pointer */
					if(mouse_track(r, 3, i == MMove, Pt(r.max.x-1, r.max.y-1), &r) == 0)
						desk_floatset(&desk, id, lrect(rectsubpt(r, screen->r.min)));
				}
				break;
			case MDelete:
				win_delete(win_lookid(id));
				break;
			case MHide:
				desk_hide(&desk, id);
				break;
			case MFloat:
				desk_togglefloat(&desk, id);
				break;
			}
	}
	mreset();
}

static void
domouse(Mouse *m)
{
	static LRect fstart;
	static int fid;
	char err[128], *act;
	Mdec d;
	Mact a;
	Rectangle r;
	Win *w;
	LRect f;
	long ms;
	int x, y, nx, ny;

	if(ovl != nil){	/* a press closes the overlay and goes no further; the routers resync on the release */
		if(m->buttons){
			overlay_close(ovl);
			ovl = nil;
			mreset();
		}
		return;
	}
	x = m->xy.x - screen->r.min.x;
	y = m->xy.y - screen->r.min.y;
	mpol(&mp, places, nplaces, x, y, m->buttons, m->msec,
		input_modheld(&in), input_heldkey(&in, Kshift), &a);
	switch(a.kind){
	case Dpass:
		mroute(&mr, places, nplaces, desk_focused(&desk), x, y, m->buttons, &d);
		if(d.focus)
			desk_focus(&desk, d.focus);
		if(d.to && (w = win_lookid(d.to)) != nil && !w->deleted){
			if(!w->q.mouseopen && term_visible(w)){
				ms = mc->msec;
				term_mouse(w, m);
				if(mc->msec != ms)	/* button2menu read the mouse up to the release */
					mreset();
			}else
				wq_mouse(&w->q, m->xy.x, m->xy.y, m->buttons, m->msec);
		}
		break;
	case Dswallow:
		if(a.id)
			desk_focus(&desk, a.id);
		break;
	case Dfmove:
		if(fid != a.id){
			fid = a.id;
			desk_focus(&desk, fid);	/* focusing raises it */
			if(!desk_floatrect(&desk, fid, &fstart))
				fid = 0;
		}
		if(fid){
			nx = fstart.x0 + a.dx;
			ny = fstart.y0 + a.dy;
			if(nx + fstart.x1 - fstart.x0 > desk.vw)
				nx = desk.vw - (fstart.x1 - fstart.x0);
			if(ny + fstart.y1 - fstart.y0 > desk.vh)
				ny = desk.vh - (fstart.y1 - fstart.y0);
			if(nx < 0)
				nx = 0;
			if(ny < 0)
				ny = 0;
			f.x0 = nx;
			f.y0 = ny;
			f.x1 = nx + fstart.x1 - fstart.x0;
			f.y1 = ny + fstart.y1 - fstart.y0;
			desk_floatset(&desk, fid, f);
		}
		if(!(m->buttons & 1))
			fid = 0;
		break;
	case Dfresize:
		if(desk_floatrect(&desk, a.id, &f)){
			r = rectaddpt(rectof(f), screen->r.min);
			if(mouse_track(r, a.corner, 0, Pt(m->xy.x - a.dx, m->xy.y - a.dy), &r) == 0)
				desk_floatset(&desk, a.id, lrect(rectsubpt(r, screen->r.min)));
		}
		mreset();
		break;
	case Dmenu:
		domenu(a.id);
		break;
	case Dwheel:
		if(a.id == Wws)
			act = a.dir < 0 ? "focus-workspace-up" : "focus-workspace-down";
		else{
			/* the strip scrolls, so the tile layer takes the focus; floats never cycle */
			if(desk.ws[desk.cur].layer == Lfloat)
				desk_switchlayer(&desk);
			if(desk.ws[desk.cur].layer == Lfloat)	/* no tiles: nothing to scroll */
				break;
			act = a.dir < 0 ? "focus-column-left" : "focus-column-right";
		}
		if(action_run(&desk, act, err, sizeof err) < 0)
			dprint("action error: %s\n", err);
		break;
	}
}

static void
dospawned(Spawned *s)
{
	Win *w;
	char buf[64];

	if(s->id == 0){	/* the startup script: its group is reached on exit */
		if(s->pid > 0){
			snprint(buf, sizeof buf, "/proc/%d/notepg", s->pid);
			startfd = open(buf, OWRITE|OCEXEC);
		}
		return;
	}
	w = win_lookid(s->id);
	if(s->pid > 0){
		win_setpid(w, s->pid, 1);
		if(w->deleted)
			win_note(w, "hangup");
	}else
		win_delete(w);
	win_decref(w);
}

void
threadmain(int argc, char **argv)
{
	Alt a[9];
	ulong tick, cgen;
	Mouse mouse;
	Req *rq;
	Faux *fx;
	Spawned sp;
	char err[128], *modspec, *center, *km;
	char cmd[64], rcfile[256];
	char *initfile;
	int rz, fd;

	threadsetname("main");
	modspec = nil;
	center = nil;
	spawncmd = nil;
	initfile = nil;
	ARGBEGIN{
	case 'D':
		debug++;
		chatty9p++;
		break;
	case 'm':
		modspec = EARGF(usage());
		break;
	case 'c':
		center = EARGF(usage());
		break;
	case 's':
		spawncmd = EARGF(usage());
		break;
	case 'i':
		initfile = EARGF(usage());
		break;
	case 'F':	/* test hook: image allocation fails from the n-th reshape on; not in usage */
		failafter = atoi(EARGF(usage()));
		break;
	default:
		usage();
	}ARGEND
	if(argc != 0)
		usage();

	bind_init(&bt);
	bind_defaults(&bt);
	if(modspec != nil && bind_setmod(&bt, modspec, err, sizeof err) < 0){
		fprint(2, "rini: -m %s: %s\n", modspec, err);
		usage();
	}
	input_init(&in, &bt);
	mp.border = Border;

	if(initdraw(nil, nil, "rini") < 0)
		sysfatal("initdraw: %r");
	mc = initmouse(nil, screen);
	if(mc == nil)
		sysfatal("initmouse: %r");
	if(render_init() < 0)
		sysfatal("render_init failed");
	if(term_init() < 0)
		sysfatal("term_init failed");
	wscreen = allocscreen(screen, background, 0);
	if(wscreen == nil)
		sysfatal("allocscreen: %r");
	draw(screen, screen->r, background, nil, ZP);
	flushimage(display, 1);

	desk_init(&desk, Dx(screen->r), Dy(screen->r), Gap);
	if(center != nil){
		snprint(cmd, sizeof cmd, "set-center %s", center);
		if(action_run(&desk, cmd, err, sizeof err) < 0){
			fprint(2, "rini: -c %s: %s\n", center, err);
			usage();
		}
	}
	if(access("/mnt/rini", AEXIST) < 0){
		fd = create("/mnt/rini", OREAD, DMDIR|0775);
		if(fd < 0)
			fprint(2, "rini: cannot create /mnt/rini: %r; mkdir /mnt/rini once\n");
		else
			close(fd);
	}
	if(fs_init() < 0)
		sysfatal("fs_init: %r");
	spawnc = chancreate(sizeof(Spawned), 8);
	confirmc = chancreate(sizeof(ulong), 0);
	relayout();
	if(initfile == nil){
		snprint(rcfile, sizeof rcfile, "%s/lib/rinistart", getenv("home"));
		if(access(rcfile, AREAD) == 0)
			initfile = rcfile;
	}
	if(initfile != nil)
		startup(initfile);
	else if(ui_action("spawn", 0, err, sizeof err) < 0)
		fprint(2, "rini: no window: %s\n", err);

	kbdc = chancreate(sizeof(char*), 32);
	proccreate(kbdproc, kbdc, 8192);

	a[0].c = mc->c;
	a[0].v = &mouse;
	a[0].op = CHANRCV;
	a[1].c = mc->resizec;
	a[1].v = &rz;
	a[1].op = CHANRCV;
	a[2].c = kbdc;
	a[2].v = &km;
	a[2].op = CHANRCV;
	a[3].c = reqc;
	a[3].v = &rq;
	a[3].op = CHANRCV;
	a[4].c = clunkc;
	a[4].v = &fx;
	a[4].op = CHANRCV;
	a[5].c = spawnc;
	a[5].v = &sp;
	a[5].op = CHANRCV;
	a[6].c = tickc;
	a[6].v = &tick;
	a[6].op = CHANRCV;
	a[7].c = confirmc;
	a[7].v = &cgen;
	a[7].op = CHANRCV;
	a[8].op = CHANEND;
	for(;;){
		switch(alt(a)){
		case 0:
			domouse(&mouse);
			break;
		case 1:
			resized();
			break;
		case 2:
			dokbd(km);
			free(km);
			break;
		case 3:
			fs_req(rq);
			break;
		case 4:
			fs_clunk(fx);
			break;
		case 5:
			dospawned(&sp);
			break;
		case 6:
			term_tick(tick);
			break;
		case 7:
			if(cgen == confirmgen && note != nil)
				notecancel();
			break;
		}
		relayout();
	}
}
