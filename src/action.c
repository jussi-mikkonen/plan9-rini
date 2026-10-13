#include "compat.h"
#include "layout.h"
#include "action.h"

enum {
	Ofocus, Ofirst, Olast, Opreset, Osetwidth, Ocenter, Osetcenter,
	Oconsume, Oexpel, Oconexpel, Ofocuswin, Omovecol, Omovewin,
	Opresetheight, Oresetheight, Osetheight, Omaximize,
	Ofocusws, Ofocuswinws, Omovecolws, Omovewinws, Omovewinorws,
	Otogglefloat, Oswitchlayer, Ofullscreen
};

typedef struct Act Act;
struct Act {
	char *name;
	int op;
	int dir;	/* -1 or 1; 0 when the action takes no direction or a workspace number */
	int tile;	/* works on tiles only */
};

static Act acts[] = {
	{"focus-column-left", Ofocus, -1, 0},
	{"focus-column-right", Ofocus, 1, 0},
	{"focus-column-first", Ofirst, 0, 1},
	{"focus-column-last", Olast, 0, 1},
	{"switch-preset-column-width", Opreset, 1, 1},
	{"switch-preset-column-width-back", Opreset, -1, 1},
	{"set-column-width", Osetwidth, 0, 1},
	{"center-column", Ocenter, 0, 1},
	{"set-center", Osetcenter, 0, 0},
	{"consume-window-into-column", Oconsume, 0, 1},
	{"expel-window-from-column", Oexpel, 0, 1},
	{"consume-or-expel-window-left", Oconexpel, -1, 1},
	{"consume-or-expel-window-right", Oconexpel, 1, 1},
	{"focus-window-down", Ofocuswin, 1, 1},
	{"focus-window-up", Ofocuswin, -1, 1},
	{"move-column-left", Omovecol, -1, 1},
	{"move-column-right", Omovecol, 1, 1},
	{"move-window-down", Omovewin, 1, 1},
	{"move-window-up", Omovewin, -1, 1},
	{"switch-preset-window-height", Opresetheight, 0, 1},
	{"reset-window-height", Oresetheight, 0, 1},
	{"set-window-height", Osetheight, 0, 1},
	{"maximize-column", Omaximize, 0, 1},
	{"focus-workspace", Ofocusws, 0, 0},
	{"focus-workspace-down", Ofocusws, 1, 0},
	{"focus-workspace-up", Ofocusws, -1, 0},
	{"focus-window-or-workspace-down", Ofocuswinws, 1, 0},
	{"focus-window-or-workspace-up", Ofocuswinws, -1, 0},
	{"move-column-to-workspace", Omovecolws, 0, 0},
	{"move-column-to-workspace-down", Omovecolws, 1, 0},
	{"move-column-to-workspace-up", Omovecolws, -1, 0},
	{"move-window-to-workspace", Omovewinws, 0, 0},
	{"move-window-to-workspace-down", Omovewinws, 1, 0},
	{"move-window-to-workspace-up", Omovewinws, -1, 0},
	{"move-window-down-or-to-workspace-down", Omovewinorws, 1, 1},
	{"move-window-up-or-to-workspace-up", Omovewinorws, -1, 1},
	{"toggle-window-floating", Otogglefloat, 0, 0},
	{"switch-focus-between-floating-and-tiling", Oswitchlayer, 0, 0},
	{"fullscreen-window", Ofullscreen, 0, 0},
	{nil, 0, 0, 0}
};

int
action_parsenum(char *s, int *out)
{
	int neg, v, nd;

	neg = 0;
	v = 0;
	nd = 0;
	if(*s == '-' || *s == '+'){
		neg = *s == '-';
		s++;
	}
	while(*s >= '0' && *s <= '9'){
		v = v*10 + (*s - '0');
		if(v > 1000000)
			return -1;
		nd++;
		s++;
	}
	if(nd == 0 || *s != '\0')
		return -1;
	*out = neg ? -v : v;
	return 0;
}

static char *centernames[] = {
	"never", "always", "on-overflow",	/* CenterNever, CenterAlways, CenterOverflow */
	nil
};

int
action_parsecenter(char *arg, int *mode)
{
	int i;

	if(arg == nil)
		return -1;
	for(i = 0; centernames[i] != nil; i++)
		if(strcmp(arg, centernames[i]) == 0){
			*mode = i;
			return 0;
		}
	return -1;
}

char *
action_centername(int mode)
{
	return centernames[mode];
}

static int
badarg(char *arg, char *err, int nerr)
{
	lsnprint(err, nerr, "bad argument: %s", arg != nil ? arg : "");
	return -1;
}

/*
 * The workspace position an action targets: the next one in dir, else the
 * 1-based number in arg. Out of range is left to the desk.
 */
static int
targetws(Desk *d, int dir, char *arg, int *idx)
{
	int n;

	if(dir != 0){
		*idx = d->cur + dir;
		return 0;
	}
	if(arg == nil || action_parsenum(arg, &n) < 0)
		return -1;
	*idx = n - 1;
	return 0;
}

/* +N% and -N% are relative, N% absolute; N in 1..100 */
static int
parsepct(char *arg, int *n, int *rel)
{
	char t[16];
	int len;

	if(arg == nil)
		return -1;
	len = strlen(arg);
	if(len < 2 || len > (int)sizeof t || arg[len - 1] != '%')
		return -1;
	memmove(t, arg, len - 1);
	t[len - 1] = '\0';
	*rel = arg[0] == '+' || arg[0] == '-';
	return action_parsenum(t, n) == 0 && *n != 0 && *n >= -100 && *n <= 100 ? 0 : -1;
}

/* relative adjusts by N*10 permille, absolute sets N*10 */
static int
setpct(Strip *s, char *arg, char *err, int nerr, void (*adj)(Strip*, int), void (*set)(Strip*, int))
{
	int n, rel;

	if(parsepct(arg, &n, &rel) < 0)
		return badarg(arg, err, nerr);
	if(rel)
		adj(s, n * 10);
	else
		set(s, n * 10);
	return 0;
}

int
action_run(Desk *d, char *act, char *err, int nerr)
{
	char buf[ACTLEN];
	char *arg;
	Strip *s;
	Act *a;
	int i, len, mode, n, rel;

	s = desk_cur(d);
	len = strlen(act);
	if(len > ACTLEN - 1){
		lsnprint(err, nerr, "action too long");
		return -1;
	}
	memmove(buf, act, len + 1);
	arg = strchr(buf, ' ');
	if(arg != nil)
		*arg++ = '\0';
	for(a = acts; a->name != nil; a++)
		if(strcmp(buf, a->name) == 0)
			break;
	if(a->name == nil){
		lsnprint(err, nerr, "unknown action: %s", buf);
		return -1;
	}
	if(d->ws[d->cur].layer == Lfloat){
		/* a bad argument is an error in either layer */
		if((a->op == Osetwidth || a->op == Osetheight) && parsepct(arg, &n, &rel) < 0)
			return badarg(arg, err, nerr);
		if(a->tile)
			return 0;
		if(a->op == Ofocus || a->op == Ofocuswinws){
			desk_floatfocus(d, a->dir);
			return 0;
		}
	}
	switch(a->op){
	case Ofocus:
		strip_focus(s, a->dir);
		break;
	case Ofirst:
		strip_focusidx(s, 0);
		break;
	case Olast:
		strip_focusidx(s, s->n - 1);
		break;
	case Opreset:
		strip_preset(s, a->dir);
		break;
	case Osetwidth:
		return setpct(s, arg, err, nerr, strip_adjust, strip_setwidth);
	case Ocenter:
		strip_center(s);
		break;
	case Osetcenter:
		if(action_parsecenter(arg, &mode) < 0)
			return badarg(arg, err, nerr);
		desk_setcenter(d, mode);
		break;
	case Oconsume:
		strip_consume(s);
		break;
	case Oexpel:
		strip_expel(s);
		break;
	case Oconexpel:
		strip_consumeorexpel(s, a->dir);
		break;
	case Ofocuswin:
		strip_focuswin(s, a->dir);
		break;
	case Omovecol:
		strip_movecol(s, a->dir);
		break;
	case Omovewin:
		strip_movewin(s, a->dir);
		break;
	case Opresetheight:
		strip_presetheight(s);
		break;
	case Oresetheight:
		strip_resetheight(s);
		break;
	case Osetheight:
		return setpct(s, arg, err, nerr, strip_adjshare, strip_setshare);
	case Omaximize:
		strip_maximize(s);
		break;
	case Ofocusws:
		if(targetws(d, a->dir, arg, &i) < 0)
			return badarg(arg, err, nerr);
		desk_focusws(d, i);
		break;
	case Ofocuswinws:
		if(strip_focuswin(s, a->dir))
			desk_focusws(d, d->cur + a->dir);
		break;
	case Omovecolws:
		if(targetws(d, a->dir, arg, &i) < 0)
			return badarg(arg, err, nerr);
		desk_movecolws(d, i);
		break;
	case Omovewinws:
		if(targetws(d, a->dir, arg, &i) < 0)
			return badarg(arg, err, nerr);
		desk_movewinws(d, i);
		break;
	case Omovewinorws:
		if(strip_movewin(s, a->dir))
			desk_movewinws(d, d->cur + a->dir);
		break;
	case Otogglefloat:
		desk_togglefloat(d, desk_focused(d));
		break;
	case Oswitchlayer:
		desk_switchlayer(d);
		break;
	case Ofullscreen:
		desk_fullscreen(d);
		break;
	}
	desk_norm(d);
	return 0;
}
