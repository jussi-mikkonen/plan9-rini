/* ctltest.c: script-driven headless driver for ctl.c, rule.c and ev.c. */
#include "compat.h"
#include "ctl.h"
#include "rule.h"
#include "ev.h"
#include "script.h"

static char *verbs[] = {"bind", "unbind", "termbind", "set", "rule", "unrule", "spawn", "action"};
static char *setnames[] = {"mod", "center", "c64return", "background"};
static Ruletab tab;

/* tok <text> */
static void
tokcmd(char *text)
{
	char *t[CTLMAXARG];
	char err[128];
	int i, n;

	n = ctl_tokenize(text, t, CTLMAXARG, err, sizeof err);
	if(n < 0){
		lprint("error %s\n", err);
		return;
	}
	lprint("tok %d\n", n);
	for(i = 0; i < n; i++)
		lprint("[%s]\n", t[i]);
}

static void
quotecmd(char *text)
{
	char buf[256];

	ctl_quote(buf, sizeof buf, text);
	lprint("quote %s\n", buf);
}

static void
parseline(char *text)
{
	Ctlcmd c;
	char err[128];
	int i;

	if(ctl_parse(text, &c, err, sizeof err) < 0){
		lprint("error %s\n", err);
		return;
	}
	lprint("verb=%s set=%s\n", verbs[c.verb], c.verb == Cset ? setnames[c.set] : "-");
	for(i = 0; i < c.narg; i++)
		lprint("arg[%d]=[%s]\n", i, c.arg[i]);
	if(c.rest != nil)
		lprint("rest=[%s]\n", c.rest);
	if(c.verb == Cset && c.set == Sbackground)
		lprint("colour=0x%08lX\n", c.colour);
}

/* parselong N: parse a line of N x's, to hit the CTLLINE cap */
static void
parselong(char *arg)
{
	static char buf[LINEMAX + 1];
	int n;

	n = atoi(arg);
	memset(buf, 'x', n);
	buf[n] = '\0';
	parseline(buf);
}

static void
colourcmd(char *arg)
{
	unsigned long v;
	char err[128];

	if(ctl_parsecolour(arg, &v, err, sizeof err) < 0)
		lprint("error %s\n", err);
	else
		lprint("colour 0x%08lX\n", v);
}

/* summary <mod> <center> <c64 0|1> <bg 0xRRGGBBAA> <focus> <ws> */
static void
summarycmd(char *args)
{
	char *t[CTLMAXARG];
	char err[128], buf[256];
	unsigned long bg;

	if(ctl_tokenize(args, t, CTLMAXARG, err, sizeof err) != 6 || ctl_parsecolour(t[3], &bg, err, sizeof err) < 0){
		lprint("error bad summary line\n");
		return;
	}
	ctl_summary(buf, sizeof buf, t[0], t[1], atoi(t[2]), bg, atoi(t[4]), atoi(t[5]));
	lprint("%s", buf);
}

/* rule <rest of ctl line> */
static void
rulecmd(char *rest)
{
	Ctlcmd c;
	char buf[CTLLINE], err[128];

	lsnprint(buf, sizeof buf, "rule %s", rest);
	if(ctl_parse(buf, &c, err, sizeof err) < 0 || rule_add(&tab, c.arg[0], c.arg + 1, c.narg - 1, err, sizeof err) < 0){
		/* regexp library wording differs per platform; goldens are shared */
		lprint("error %.*s\n", strncmp(err, "bad regexp:", 11) == 0 ? 11 : 127, err);
		return;
	}
	lprint("ok\n");
}

/* unrule <regexp> */
static void
unrulecmd(char *rest)
{
	Ctlcmd c;
	char buf[CTLLINE], err[128];

	lsnprint(buf, sizeof buf, "unrule %s", rest);
	if(ctl_parse(buf, &c, err, sizeof err) < 0 || rule_del(&tab, c.arg[0], err, sizeof err) < 0){
		lprint("error %s\n", err);
		return;
	}
	lprint("ok\n");
}

/* match <subject> */
static void
matchcmd(char *subject)
{
	Props p;

	rule_match(&tab, subject, &p);
	lprint("match %s:", subject);
	if(p.set == 0)
		lprint(" none");
	if(p.set & Pfloat)
		lprint(" float");
	if(p.set & Ptile)
		lprint(" tile");
	if(p.set & Pwidth)
		lprint(" width=%d%s", p.width, p.widthpx ? "px" : "");
	if(p.set & Pws)
		lprint(" ws=%d", p.ws);
	if(p.set & Prect)
		lprint(" rect=%d,%d,%d,%d", p.rect[0], p.rect[1], p.rect[2], p.rect[3]);
	lprint("\n");
}

static void
rulescmd(void)
{
	char buf[CTLLINE];
	int i;

	for(i = 0; i < tab.n; i++){
		rule_fmt(&tab.r[i], buf, sizeof buf);
		lprint("%s\n", buf);
	}
}

/* event fixtures: com is the committed snap, pend the one being edited */
static Snap com, pend;
static Evq rd[2];
static int nrd;
static Tapq tq;
static int tapid;

static char*
word(char **pp)
{
	char *p, *w;

	p = *pp;
	while(*p == ' ')
		p++;
	w = p;
	while(*p != '\0' && *p != ' ')
		p++;
	if(*p != '\0')
		*p++ = '\0';
	*pp = p;
	return w;
}

/* hex pairs to bytes; returns the count */
static int
unhex(char *s, char *out, int max)
{
	char t[3];
	int n;

	t[2] = '\0';
	for(n = 0; n < max && s[2*n] != '\0'; n++){
		t[0] = s[2*n];
		t[1] = s[2*n+1];
		out[n] = strtol(t, nil, 16);
	}
	return n;
}

/* snap <nws> <cur 1-based> <focus> */
static void
snapcmd(char *a)
{
	pend.nws = atoi(word(&a));
	pend.cur = atoi(word(&a)) - 1;
	pend.focus = atoi(word(&a));
}

/* win <id> <ws 1-based> tile|float|hid <wmil> <label>; winhex takes the label in hex */
static void
wincmd(char *a, int hex)
{
	Snapwin *w;
	char *kind;
	int id, i;

	id = atoi(word(&a));
	for(i = 0; i < pend.n && pend.w[i].id != id; i++)
		;
	if(i == pend.n)
		pend.n++;
	w = &pend.w[i];
	w->id = id;
	w->ws = atoi(word(&a)) - 1;
	kind = word(&a);
	w->kind = strcmp(kind, "float") == 0 ? Kfloat : strcmp(kind, "hid") == 0 ? Khid : Ktile;
	w->wmil = atoi(word(&a));
	if(hex)
		w->label[unhex(word(&a), w->label, EVLABEL-1)] = '\0';
	else
		lsnprint(w->label, EVLABEL, "%s", a);
}

static void
unwincmd(char *a)
{
	int id, i;

	id = atoi(a);
	for(i = 0; i < pend.n; i++)
		if(pend.w[i].id == id){
			memmove(&pend.w[i], &pend.w[i+1], (pend.n - i - 1) * sizeof pend.w[0]);
			pend.n--;
			return;
		}
}

/* arg nil: a live event, shown and queued for every reader; else a snapshot line for one reader */
static void
emit(char *l, void *arg)
{
	int i;

	if(arg != nil){
		evq_push(arg, l);
		return;
	}
	lprint("ev %s\n", l);
	for(i = 0; i < nrd; i++)
		evq_push(&rd[i], l);
}

static void
commitcmd(void)
{
	ev_diff(&com, &pend, emit, nil);
	memmove(&com, &pend, sizeof com);
}

static void
readercmd(void)
{
	if(nrd == 2){
		lprint("error two readers at most\n");
		return;
	}
	memset(&rd[nrd], 0, sizeof rd[0]);
	ev_snapshot(&com, emit, &rd[nrd]);
	lprint("reader %d\n", nrd++);
}

/* bytes with newline and NUL made visible */
static void
show(char *b, int n)
{
	int i;

	for(i = 0; i < n; i++){
		if(b[i] == '\n')
			lprint("\\n");
		else if(b[i] == '\0')
			lprint("\\0");
		else
			lprint("%c", b[i]);
	}
}

/* read <k> <n> */
static void
readcmd(char *a)
{
	static char b[4096];
	Evq *q;
	int n;

	q = &rd[atoi(word(&a))];
	n = evq_read(q, b, atoi(word(&a)));
	lprint("read %d [", n);
	show(b, n);
	lprint("]\n");
}

/* drain <k> [max]: the first max (12) lines in full, then a count and the last line */
static void
draincmd(char *a)
{
	static char b[4096], last[4096];
	Evq *q;
	int n, lines, ovl, ln, max;

	q = &rd[atoi(word(&a))];
	max = *a != '\0' ? atoi(a) : 12;
	lines = ovl = ln = 0;
	while((n = evq_read(q, b, sizeof b)) > 0){
		if(strncmp(b, "overflow\n", 9) == 0)
			ovl++;
		if(lines++ < max){
			lprint("line [");
			show(b, n);
			lprint("]\n");
		}
		memmove(last, b, n);
		ln = n;
	}
	lprint("lines %d overflow %d last [", lines, ovl);
	show(last, ln);
	lprint("]\n");
}

/* evpush <k> <count> <text>: pushes "<text> <i>" count times */
static void
evpushcmd(char *a)
{
	char l[EVLINE];
	Evq *q;
	int i, n;

	q = &rd[atoi(word(&a))];
	n = atoi(word(&a));
	for(i = 0; i < n; i++){
		lsnprint(l, sizeof l, "%s %d", a, i);
		evq_push(q, l);
	}
}

static void
statecmd(void)
{
	static char b[MAXWIN*(EVLINE+8) + 256];

	ev_statefmt(&com, b, sizeof b);
	lprint("%s", b);
}

/* tappush <msg> */
static void
tappushcmd(char *m)
{
	if(tap_push(&tq, tapid, m, strlen(m)) < 0)
		lprint("full\n");
}

/* tapread [n] */
static void
tapreadcmd(char *a)
{
	char b[KMSGLEN];
	int n;

	n = tap_read(&tq, b, *a != '\0' ? atoi(a) : KMSGLEN);
	if(n < 0)
		lprint("short\n");
	else if(n == 0)
		lprint("empty\n");
	else{
		lprint("tap %d [", n);
		show(b, n);
		lprint("]\n");
	}
}

/* tapsplit <hex> */
static void
tapsplitcmd(char *a)
{
	char d[256], err[64], *m[8];
	int len[8], n, i;

	n = tap_split(d, unhex(a, d, sizeof d), m, len, 8, err, sizeof err);
	if(n < 0){
		lprint("error %s\n", err);
		return;
	}
	lprint("split %d\n", n);
	for(i = 0; i < n; i++){
		lprint("msg %d [", len[i]);
		show(m[i], len[i]);
		lprint("]\n");
	}
}

static void
cmd(char *line)
{
	lprint("> %s\n", line);
	if(strncmp(line, "tok ", 4) == 0)
		tokcmd(line + 4);
	else if(strncmp(line, "quote ", 6) == 0)
		quotecmd(line + 6);
	else if(strncmp(line, "parse ", 6) == 0)
		parseline(line + 6);
	else if(strncmp(line, "parselong ", 10) == 0)
		parselong(line + 10);
	else if(strncmp(line, "colour ", 7) == 0)
		colourcmd(line + 7);
	else if(strncmp(line, "summary ", 8) == 0)
		summarycmd(line + 8);
	else if(strncmp(line, "rule ", 5) == 0)
		rulecmd(line + 5);
	else if(strncmp(line, "unrule ", 7) == 0)
		unrulecmd(line + 7);
	else if(strncmp(line, "match ", 6) == 0)
		matchcmd(line + 6);
	else if(strcmp(line, "rules") == 0)
		rulescmd();
	else if(strcmp(line, "ruleclear") == 0)
		memset(&tab, 0, sizeof tab);
	else if(strncmp(line, "snap ", 5) == 0)
		snapcmd(line + 5);
	else if(strncmp(line, "win ", 4) == 0)
		wincmd(line + 4, 0);
	else if(strncmp(line, "winhex ", 7) == 0)
		wincmd(line + 7, 1);
	else if(strncmp(line, "unwin ", 6) == 0)
		unwincmd(line + 6);
	else if(strcmp(line, "commit") == 0)
		commitcmd();
	else if(strcmp(line, "reader") == 0)
		readercmd();
	else if(strncmp(line, "read ", 5) == 0)
		readcmd(line + 5);
	else if(strncmp(line, "drain ", 6) == 0)
		draincmd(line + 6);
	else if(strncmp(line, "evpush ", 7) == 0)
		evpushcmd(line + 7);
	else if(strcmp(line, "state") == 0)
		statecmd();
	else if(strncmp(line, "tapfocus ", 9) == 0)
		tapid = atoi(line + 9);
	else if(strncmp(line, "tappush ", 8) == 0)
		tappushcmd(line + 8);
	else if(strcmp(line, "tapread") == 0)
		tapreadcmd("");
	else if(strcmp(line, "tapfrom") == 0)
		lprint("from %d\n", tq.from);
	else if(strncmp(line, "tapread ", 8) == 0)
		tapreadcmd(line + 8);
	else if(strncmp(line, "tapsplit ", 9) == 0)
		tapsplitcmd(line + 9);
	else
		lprint("error unknown command: %s\n", line);
}

static int
run(void)
{
	com.cur = com.focus = pend.cur = pend.focus = -1;
	tap_init(&tq);
	return script(LINEMAX, cmd);
}
