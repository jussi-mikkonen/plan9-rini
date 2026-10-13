/* kbdtest.c: script-driven headless driver for the binding table and kbd router. */
#include "compat.h"
#define HEXHELP
#define KMSGHELP
#include "keys.h"
#include "input.h"
#include "script.h"

static Bindtab bt;
static Input in;
static Kout out;
static long now;	/* ms, set by t */

static void
show(void)
{
	int i, n, r;

	for(i = 0; i < out.nact; i++)
		lprint("act %s\n", out.act[i]);
	if(out.len == 0)
		return;
	lprint("fwd %c", out.msg[0]);
	for(i = 1; i < out.len; i += n){
		n = input_utf8dec(out.msg + i, out.len - i, &r);
		if(n < 0){
			lprint(" ??");
			break;
		}
		lprint(" %X", r);
	}
	lprint("\n");
}

static void
send(char *msg, int len)
{
	if(input_kbd(&in, msg, len, now, &out) < 0)
		lprint("drop malformed\n");
	else
		show();
}

/* k/K/c line: type then hex runes */
static void
fixture(char *line)
{
	char msg[KMSGLEN + 16];
	int len;

	len = kmsg(line, msg, sizeof msg);
	if(len < 0)
		lprint("error bad fixture line\n");
	else
		send(msg, len);
}

static void
rawline(char *line)
{
	char msg[KMSGLEN + 16];
	char *p;
	long v;
	int len;

	len = 0;
	p = line + 2;
	for(;;){
		while(*p == ' ')
			p++;
		if(*p == '\0')
			break;
		v = nexthex(&p);
		if(v < 0 || v > 255 || len >= (int)sizeof msg){
			lprint("error bad raw line\n");
			return;
		}
		msg[len++] = (char)v;
	}
	send(msg, len);
}

static void
longline(char *line)
{
	static char msg[1100];
	int n, i;
	char *p;

	n = 0;
	for(p = line + 5; *p >= '0' && *p <= '9'; p++)
		n = n*10 + (*p - '0');
	if(n < 0 || n > 1024){
		lprint("error bad length\n");
		return;
	}
	msg[0] = 'k';
	for(i = 0; i < n; i++)
		msg[1 + i] = 'a';
	send(msg, n + 1);
}

static void
list(void)
{
	char buf[ACTLEN + 64];
	int i;

	for(i = 0; i < bt.n; i++){
		bind_line(&bt.b[i], buf, sizeof buf);
		lprint("%s\n", buf);
	}
}

static void
bindline(char *args)
{
	char err[128];
	char *chord, *pol, *act;

	chord = args;
	pol = chord;
	while(*pol != '\0' && *pol != ' ')
		pol++;
	if(*pol == '\0'){
		lprint("error bad argument: %s\n", args);
		return;
	}
	*pol++ = '\0';
	act = pol;
	while(*act != '\0' && *act != ' ')
		act++;
	if(*act == '\0'){
		lprint("error bad argument: %s\n", args);
		return;
	}
	*act++ = '\0';
	if(bind_add(&bt, chord, pol, act, err, sizeof err) < 0)
		lprint("error %s\n", err);
	else
		lprint("ok\n");
}

static void
cmd(char *line)
{
	char err[128];

	lprint("> %s\n", line);
	if(strncmp(line, "mod ", 4) == 0){
		if(bind_setmod(&bt, line + 4, err, sizeof err) < 0)
			lprint("error %s\n", err);
		else
			lprint("ok\n");
	}else if(strncmp(line, "bind ", 5) == 0)
		bindline(line + 5);
	else if(strcmp(line, "defaults") == 0)
		bind_defaults(&bt);
	else if(strcmp(line, "list") == 0 || strcmp(line, "lines") == 0)
		list();
	else if(strncmp(line, "t ", 2) == 0)
		now = strtol(line + 2, nil, 10);
	else if(strcmp(line, "modheld") == 0)
		lprint("modheld %d\n", input_modheld(&in));
	else if(strcmp(line, "modfmt") == 0){
		bind_modfmt(&bt, err, sizeof err);
		lprint("%s\n", err);
	}else if(strncmp(line, "unbind ", 7) == 0){
		if(bind_del(&bt, line + 7, err, sizeof err) < 0)
			lprint("error %s\n", err);
		else
			lprint("ok\n");
	}
	else if(strncmp(line, "long ", 5) == 0)
		longline(line);
	else if(strncmp(line, "x ", 2) == 0)
		rawline(line);
	else if(strchr("kKcq", line[0]) != nil && (line[1] == ' ' || line[1] == '\0'))
		fixture(line);	/* q is an unknown type: the router must reject it */
	else
		lprint("error unknown command: %s\n", line);
}

static int
run(void)
{
	bind_init(&bt);
	input_init(&in, &bt);
	return script(LINEMAX, cmd);
}
