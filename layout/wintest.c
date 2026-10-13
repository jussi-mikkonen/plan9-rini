/* wintest.c: script-driven headless driver for wctl.c and wq.c. */
#include "compat.h"
#define HEXHELP
#define DUMPHELP
#define KMSGHELP
#include "wctl.h"
#include "wq.h"
#include "script.h"

#define READMAX 512

static void
canon(Wctl *w)
{
	lprint("cmd=%s", wctl_cmdname(w->cmd));
	if(w->set & Pr)
		lprint(" r=%d,%d,%d,%d", w->r[0], w->r[1], w->r[2], w->r[3]);
	if(w->set & Pdx)
		lprint(" dx=%d", w->dx);
	if(w->set & Pdy)
		lprint(" dy=%d", w->dy);
	if(w->set & Pminx)
		lprint(" minx=%s%d", (w->rel & Pminx) && w->minx > 0 ? "+" : "", w->minx);
	if(w->set & Pminy)
		lprint(" miny=%s%d", (w->rel & Pminy) && w->miny > 0 ? "+" : "", w->miny);
	if(w->set & Pmaxx)
		lprint(" maxx=%s%d", (w->rel & Pmaxx) && w->maxx > 0 ? "+" : "", w->maxx);
	if(w->set & Pmaxy)
		lprint(" maxy=%s%d", (w->rel & Pmaxy) && w->maxy > 0 ? "+" : "", w->maxy);
	if(w->set & Phide)
		lprint(" hide");
	if(w->set & Pscroll)
		lprint(" scroll");
	if(w->set & Pnoscroll)
		lprint(" noscroll");
	if(w->set & Pcd)
		lprint(" cd=%s", w->cd);
	if(w->set & Ppid)
		lprint(" pid=%d", w->pid);
	if(w->set & Pid)
		lprint(" id=%d", w->id);
	if(w->rest != nil)
		lprint(" rest=%s", w->rest);
	lprint("\n");
}

static void
wctlcmd(char *text)
{
	Wctl w;
	char err[128];

	if(wctl_parse(text, &w, err, sizeof err) < 0)
		lprint("error %s\n", err);
	else
		canon(&w);
}

static void
attachcmd(char *aname)
{
	Wctl w;
	char err[128];
	int id, k;

	k = wctl_attach(aname, &w, &id, err, sizeof err);
	if(k < 0)
		lprint("error %s\n", err);
	else if(k == Anone)
		lprint("attach none\n");
	else if(k == Actl)
		lprint("attach ctl\n");
	else if(k == Aid)
		lprint("attach id=%d\n", id);
	else{
		lprint("attach new ");
		canon(&w);
	}
}

/* rect x0 y0 x1 y1 sx0 sy0 sx1 sy1 <wctl command> */
static void
rectcmd(char *args)
{
	Wctl w;
	LRect base, scr, r;
	char err[128];
	int v[8], i;

	for(i = 0; i < 8; i++)
		v[i] = strtol(args, &args, 10);
	base.x0 = v[0]; base.y0 = v[1]; base.x1 = v[2]; base.y1 = v[3];
	scr.x0 = v[4]; scr.y0 = v[5]; scr.x1 = v[6]; scr.y1 = v[7];
	if(wctl_parse(args, &w, err, sizeof err) < 0)
		lprint("error %s\n", err);
	else{
		wctl_rect(&w, base, scr, &r);
		lprint("rect %d %d %d %d\n", r.x0, r.y0, r.x1, r.y1);
	}
}

/* newwidth sx0 sy0 sx1 sy1 <wctl command> */
static void
newwidthcmd(char *args)
{
	Wctl w;
	LRect scr;
	char err[128];
	int v[4], i;

	for(i = 0; i < 4; i++)
		v[i] = strtol(args, &args, 10);
	scr.x0 = v[0]; scr.y0 = v[1]; scr.x1 = v[2]; scr.y1 = v[3];
	if(wctl_parse(args, &w, err, sizeof err) < 0)
		lprint("error %s\n", err);
	else
		lprint("newwidth %d\n", wctl_newwidth(&w, scr));
}

/* fmt x0 y0 x1 y1 s1 s2 */
static void
fmtcmd(char *args)
{
	char buf[WCTLLEN + 64];
	char *s1, *s2, *p;
	int v[4], i, n;

	p = args;
	for(i = 0; i < 4; i++)
		v[i] = strtol(p, &p, 10);
	while(*p == ' ')
		p++;
	s1 = p;
	while(*p != '\0' && *p != ' ')
		p++;
	if(*p == '\0'){
		lprint("error bad fmt line\n");
		return;
	}
	*p++ = '\0';
	s2 = p;
	n = wctl_fmt(buf, sizeof buf, v[0], v[1], v[2], v[3], s1, s2);
	lprint("[%s] %d\n", buf, n);
}

static Wq q[4];
static Place pl[8];
static int npl, rfocus;
static Mroute mr;
static Mpol mp;

/* route X Y B: print the decision, then apply its focus change like the caller does */
static void
routecmd(char *args)
{
	Mdec d;
	long v[3];

	if(ints(args, v, 3) != 3){
		lprint("error bad route line\n");
		return;
	}
	mroute(&mr, pl, npl, rfocus, (int)v[0], (int)v[1], (int)v[2], &d);
	lprint("to=%d focus=%d swallow=%d grab=%d\n", d.to, d.focus, mr.swallow, mr.grab);
	if(d.focus != 0)
		rfocus = d.focus;
}

/* place ID X0 Y0 X1 Y1 [t|F|f [full|part|off|parked]] */
static void
placecmd(char *args)
{
	long v[5];
	char *p;
	int i;

	if(ints(args, v, 5) != 5 || npl >= 8){
		lprint("error bad place line\n");
		return;
	}
	pl[npl].id = (int)v[0];
	pl[npl].s.x0 = (int)v[1];
	pl[npl].s.y0 = (int)v[2];
	pl[npl].s.x1 = (int)v[3];
	pl[npl].s.y1 = (int)v[4];
	pl[npl].layer = Ltile;
	pl[npl].vis = VisFull;
	p = args;
	for(i = 0; i < 5; i++)
		strtol(p, &p, 10);
	while(*p == ' ')
		p++;
	if(*p == 'F')
		pl[npl].layer = Lfs;
	else if(*p == 'f')
		pl[npl].layer = Lfloat;
	while(*p != ' ' && *p != '\0')
		p++;
	while(*p == ' ')
		p++;
	if(strcmp(p, "part") == 0)
		pl[npl].vis = VisPart;
	else if(strcmp(p, "off") == 0)
		pl[npl].vis = VisOff;
	else if(strcmp(p, "parked") == 0)
		pl[npl].vis = VisParked;
	npl++;
}

static void
hitcmd(char *args)
{
	long v[2];
	int i;

	if(ints(args, v, 2) != 2){
		lprint("error bad hit line\n");
		return;
	}
	i = place_hit(pl, npl, (int)v[0], (int)v[1]);
	lprint("hit %d\n", i < 0 ? 0 : pl[i].id);
}

/* mpol X Y BUTTONS MSEC MOD SHIFT */
static void
mpolcmd(char *args)
{
	static char *names[] = {"Dpass", "Dswallow", "Dmenu", "Dfmove", "Dfresize", "Dwheel"};
	Mact a;
	long v[6];

	if(ints(args, v, 6) != 6){
		lprint("error bad mpol line\n");
		return;
	}
	mpol(&mp, pl, npl, (int)v[0], (int)v[1], (int)v[2], v[3], (int)v[4], (int)v[5], &a);
	lprint("kind=%s id=%d dir=%d corner=%d dx=%d dy=%d\n", names[a.kind], a.id, a.dir, a.corner, a.dx, a.dy);
}

/* q I kbd <type> <hex runes...> */
static void
kbdcmd(Wq *w, char *args)
{
	char msg[KMSGLEN + 16];
	int len;

	len = kmsg(args, msg, sizeof msg);
	if(len < 0)
		lprint("error bad rune\n");
	else
		wq_kbd(w, msg, len);
}

static void
readcmd(Wq *w, char *args)
{
	char buf[READMAX + 8];
	long v[1];
	int n;

	if(strncmp(args, "mouse", 5) == 0){
		if(!wq_mouseready(w))
			lprint("notready\n");
		else{
			wq_mouseread(w, buf);
			lprint("[%s]\n", buf);
		}
		return;
	}
	if(ints(args + 4, v, 1) != 1 || v[0] > READMAX){
		lprint("error bad read size\n");
		return;
	}
	if(!wq_kbdready(w))
		lprint("notready\n");
	else{
		n = wq_kbdread(w, buf, (int)v[0]);
		dump("kbd", buf, n);
	}
}

/* q I <verb> args */
static void
qcmd(char *line)
{
	Wq *w;
	char *verb, *a;
	long v[4];
	int x, y;

	if(line[0] < '0' || line[0] > '3' || line[1] != ' '){
		lprint("error bad window\n");
		return;
	}
	w = &q[line[0] - '0'];
	verb = line + 2;
	a = strchr(verb, ' ');
	if(a != nil)
		*a++ = '\0';
	else
		a = verb + strlen(verb);
	if(strcmp(verb, "init") == 0)
		memset(w, 0, sizeof *w);
	else if(strcmp(verb, "open") == 0 || strcmp(verb, "close") == 0){
		x = verb[0] == 'o';
		if(strcmp(a, "mouse") == 0)
			wq_mouseopen(w, x);
		else
			wq_kbdopen(w, x);
	}else if(strcmp(verb, "delta") == 0 && ints(a, v, 2) == 2)
		wq_moved(w, (int)v[0], (int)v[1]);
	else if(strcmp(verb, "mouse") == 0 && ints(a, v, 4) == 4)
		wq_mouse(w, (int)v[0], (int)v[1], (int)v[2], v[3]);
	else if(strcmp(verb, "reshape") == 0)
		wq_reshaped(w);
	else if(strcmp(verb, "unfocus") == 0)
		wq_unfocus(w);
	else if(strcmp(verb, "kbd") == 0)
		kbdcmd(w, a);
	else if(strcmp(verb, "warp") == 0){
		if(wq_warp(w, a, strlen(a), &x, &y) < 0)
			lprint("error bad format on /dev/mouse\n");
		else
			lprint("warp %d %d\n", x, y);
	}else if(strcmp(verb, "read") == 0)
		readcmd(w, a);
	else
		lprint("error bad window command: %s\n", verb);
}

static void
cmd(char *line)
{
	lprint("> %s\n", line);
	if(strncmp(line, "wctl ", 5) == 0)
		wctlcmd(line + 5);
	else if(strncmp(line, "attach", 6) == 0 && (line[6] == '\0' || line[6] == ' '))
		attachcmd(line[6] == ' ' ? line + 7 : line + 6);
	else if(strncmp(line, "rect ", 5) == 0)
		rectcmd(line + 5);
	else if(strncmp(line, "newwidth ", 9) == 0)
		newwidthcmd(line + 9);
	else if(strncmp(line, "fmt ", 4) == 0)
		fmtcmd(line + 4);
	else if(strcmp(line, "clear") == 0){
		npl = 0;
		memset(&mr, 0, sizeof mr);
		memset(&mp, 0, sizeof mp);
		mp.border = 4;
	}else if(strncmp(line, "hit ", 4) == 0)
		hitcmd(line + 4);
	else if(strncmp(line, "mreset ", 7) == 0)
		mroute_reset(&mr, atoi(line + 7));
	else if(strncmp(line, "mpol ", 5) == 0)
		mpolcmd(line + 5);
	else if(strncmp(line, "preset ", 7) == 0)
		mpol_reset(&mp, atoi(line + 7));
	else if(strncmp(line, "place ", 6) == 0)
		placecmd(line + 6);
	else if(strncmp(line, "focus ", 6) == 0)
		rfocus = atoi(line + 6);
	else if(strncmp(line, "route ", 6) == 0)
		routecmd(line + 6);
	else if(strncmp(line, "q ", 2) == 0)
		qcmd(line + 2);
	else
		lprint("error unknown command: %s\n", line);
}

static int
run(void)
{
	return script(LINEMAX, cmd);
}
