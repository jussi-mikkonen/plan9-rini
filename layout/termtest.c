/* termtest.c: script-driven headless driver for tkeys.c. */
#include "compat.h"
#define HEXHELP
#define DUMPHELP
#include "keys.h"
#include "tkeys.h"
#include "input.h"
#include "tbuf.h"
#include "script.h"

static Tkeytab kt;
static Tbuf tb;
static int c64 = 1;
static char upart[TUTFMAX];
static int nupart;

/* k add KEY ACTION */
static void
addcmd(char *args)
{
	char err[128];
	char *act;

	act = strchr(args, ' ');
	if(act == nil){
		lprint("error bad k add line\n");
		return;
	}
	*act++ = '\0';
	if(tkeys_add(&kt, args, act, err, sizeof err) < 0)
		lprint("error %s\n", err);
}

/* k look SHIFT HEXRUNE */
static void
lookcmd(char *args)
{
	long sh, r;
	char *p;

	p = args;
	sh = nexthex(&p);
	r = nexthex(&p);
	if(sh < 0 || r < 0){
		lprint("error bad k look line\n");
		return;
	}
	lprint("act=%s\n", tkeys_actname(tkeys_lookup(&kt, (int)sh, (int)r)));
}

static void
dumpcmd(void)
{
	int i;

	lprint("c64=%d n=%d\n", kt.c64, kt.n);
	for(i = 0; i < kt.n; i++)
		lprint("%d %X %s\n", kt.k[i].shift, kt.k[i].rune, tkeys_actname(kt.k[i].act));
}

static void
kcmd(char *line)
{
	if(strcmp(line, "init") == 0)
		tkeys_init(&kt);
	else if(strcmp(line, "defaults") == 0)
		tkeys_defaults(&kt);
	else if(strncmp(line, "add ", 4) == 0)
		addcmd(line + 4);
	else if(strncmp(line, "look ", 5) == 0)
		lookcmd(line + 5);
	else if(strncmp(line, "c64 ", 4) == 0)
		kt.c64 = atoi(line + 4);
	else if(strcmp(line, "dump") == 0)
		dumpcmd();
	else
		lprint("error unknown command: k %s\n", line);
}

/* hex runes from p; returns the count, at most max */
static long
hexrunes(char *p, Rune *r, long max)
{
	long n, v;

	for(n = 0; n < max && (v = nexthex(&p)) >= 0; n++)
		r[n] = v;
	return n;
}

static void
showflags(int f)
{
	static char *name[] = {"wake", "intr", "hold", "complete", "show"};
	int i, any;

	any = 0;
	lprint("flags=");
	for(i = 0; i < 5; i++)
		if(f & 1<<i){
			lprint("%s%s", any ? "|" : "", name[i]);
			any = 1;
		}
	lprint("%s\n", any ? "" : "none");
}

static void
showrunes(Rune *r, long n)
{
	long i;

	lprint("[");
	for(i = 0; i < n; i++)
		lprint("%s%X", i ? " " : "", r[i]);
	lprint("]");
}

static void
showcmd(void)
{
	static char buf[4*4096];
	char e[TUTFMAX];
	long i;
	int n, w;

	lprint("nr=%ld qh=%ld q0=%ld q1=%ld rawing=%d holding=%d nraw=%ld chg=%ld trimmed=%ld\n",
		tb.nr, tb.qh, tb.q0, tb.q1, tb.rawing, tb.holding, tb.nraw, tb.chg, tb.trimmed);
	n = 0;
	for(i = 0; i < tb.nr && n + TUTFMAX <= (int)sizeof buf; i++){
		w = input_utf8enc(tb.r[i], e, TUTFMAX);
		memmove(buf + n, e, w);
		n += w;
	}
	dump("text", buf, n);
}

static void
readcmd(char *args)
{
	static char buf[4096];
	long v[1];
	int n;

	if(ints(args, v, 1) != 1 || v[0] > (long)sizeof buf){
		lprint("error bad t read line\n");
		return;
	}
	if(!tbuf_ready(&tb)){
		lprint("notready\n");
		return;
	}
	n = tbuf_read(&tb, buf, v[0]);
	dump("cons", buf, n);
}

static void
ctlcmd(char *args)
{
	static char *name[] = {"rawon", "rawoff", "holdon", "holdoff", "reset"};
	int i;

	for(i = 0; i < 5; i++)
		if(strcmp(args, name[i]) == 0){
			showflags(tbuf_ctl(&tb, i));
			return;
		}
	lprint("error unknown t ctl: %s\n", args);
}

static void
rawcmd(char *args)
{
	static Rune r[LINEMAX];

	showflags(tbuf_rawkey(&tb, r, hexrunes(args, r, LINEMAX)));
}

/* t key ACTION [HEXRUNE] */
static void
keycmd(char *args)
{
	char *p;
	long r;
	int act;

	p = strchr(args, ' ');
	if(p != nil)
		*p++ = '\0';
	act = tkeys_action(args);
	if(act < 0){
		lprint("error unknown action: %s\n", args);
		return;
	}
	r = 0;
	if(p != nil && (r = nexthex(&p)) < 0)
		r = 0;
	showflags(tbuf_key(&tb, act, r, c64));
}

static void
dclickcmd(char *args)
{
	long v[1], q0, q1;

	if(ints(args, v, 1) != 1){
		lprint("error bad t dclick line\n");
		return;
	}
	tbuf_dclick(&tb, v[0], &q0, &q1);
	lprint("sel %ld %ld\n", q0, q1);
}

static void
tcmd(char *line)
{
	static Rune r[LINEMAX];
	char *p;
	long v[2], n;

	if(strncmp(line, "init ", 5) == 0 && ints(line + 5, v, 2) == 2){
		tbuf_free(&tb);
		tbuf_init(&tb, v[0], v[1]);
	}else if(strncmp(line, "out ", 4) == 0)
		tbuf_output(&tb, r, hexrunes(line + 4, r, LINEMAX));
	else if(strncmp(line, "ins ", 4) == 0 && ints(line + 4, v, 1) == 1){
		strtol(line + 4, &p, 10);	/* past the number ints read */
		n = hexrunes(p, r, LINEMAX);
		tbuf_insert(&tb, v[0], r, n);
	}else if(strncmp(line, "del ", 4) == 0 && ints(line + 4, v, 2) == 2)
		tbuf_delete(&tb, v[0], v[1]);
	else if(strncmp(line, "sel ", 4) == 0 && ints(line + 4, v, 2) == 2){
		tb.q0 = v[0];
		tb.q1 = v[1];
	}else if(strcmp(line, "ready") == 0)
		lprint("%s\n", tbuf_ready(&tb) ? "ready" : "notready");
	else if(strncmp(line, "read ", 5) == 0)
		readcmd(line + 5);
	else if(strncmp(line, "ctl ", 4) == 0)
		ctlcmd(line + 4);
	else if(strncmp(line, "raw ", 4) == 0)
		rawcmd(line + 4);
	else if(strncmp(line, "backnl ", 7) == 0 && ints(line + 7, v, 2) == 2)
		lprint("%ld\n", tbuf_backnl(&tb, v[0], v[1]));
	else if(strncmp(line, "bol ", 4) == 0 && ints(line + 4, v, 1) == 1)
		lprint("%ld\n", tbuf_bol(&tb, v[0]));
	else if(strncmp(line, "eol ", 4) == 0 && ints(line + 4, v, 1) == 1)
		lprint("%ld\n", tbuf_eol(&tb, v[0]));
	else if(strncmp(line, "key ", 4) == 0)
		keycmd(line + 4);
	else if(strncmp(line, "c64 ", 4) == 0)
		c64 = atoi(line + 4);
	else if(strncmp(line, "dclick ", 7) == 0)
		dclickcmd(line + 7);
	else if(strncmp(line, "wordstart ", 10) == 0 && ints(line + 10, v, 1) == 1)
		lprint("%ld\n", tbuf_wordstart(&tb, v[0]));
	else if(strcmp(line, "send") == 0)
		showflags(tbuf_send(&tb));
	else if(strcmp(line, "prompt") == 0){
		lprint("prompt n=%d ", tb.nprompt);
		showrunes(tb.prompt, tb.nprompt);
		lprint("\n");
	}else if(strcmp(line, "show") == 0)
		showcmd();
	else if(strcmp(line, "clean") == 0)
		tbuf_clean(&tb);
	else
		lprint("error unknown command: t %s\n", line);
}

/* u reset | u <hex bytes>: tbuf_utf8 with one persistent carry */
static void
ucmd(char *line)
{
	char s[LINEMAX];
	Rune out[LINEMAX + TUTFMAX];
	char *p;
	long v, n;

	if(strcmp(line, "reset") == 0){
		nupart = 0;
		return;
	}
	p = line;
	for(n = 0; n < LINEMAX && (v = nexthex(&p)) >= 0; n++)
		s[n] = v;
	v = tbuf_utf8(upart, &nupart, s, n, out);
	lprint("runes=%ld ", v);
	showrunes(out, v);
	lprint(" carry=%d\n", nupart);
}

static void
cmd(char *line)
{
	lprint("> %s\n", line);
	if(strncmp(line, "k ", 2) == 0)
		kcmd(line + 2);
	else if(strncmp(line, "t ", 2) == 0)
		tcmd(line + 2);
	else if(strncmp(line, "u ", 2) == 0)
		ucmd(line + 2);
	else
		lprint("error unknown command: %s\n", line);
}

static int
run(void)
{
	return script(LINEMAX, cmd);
}
