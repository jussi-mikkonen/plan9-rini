/* test.c: script-driven headless driver for the layout core. */
#include "compat.h"
#include "layout.h"
#include "action.h"
#include "script.h"

static Desk d;
static int sized;

static void
dump(Strip *s)
{
	Place p[MAXPLACE];
	int i, n;
	char *v;

	n = strip_place(s, p);
	lprint("view %d focus %d n %d\n", s->viewx, strip_focused(s), s->n);
	for(i = 0; i < n; i++){
		v = p[i].vis == VisFull ? "full" : p[i].vis == VisPart ? "part" : "off";
		if(s->c[p[i].col].n == 1)
			lprint("col %d w %d x %d px %d scr %d %d %d %d %s%s\n", p[i].id, p[i].wmil,
				p[i].x, p[i].px, p[i].s.x0, p[i].s.y0, p[i].s.x1, p[i].s.y1, v,
				p[i].focused ? "*" : "");
		else
			lprint("win %d c %d w %d x %d px %d scr %d %d %d %d %s%s\n", p[i].id, p[i].col,
				p[i].wmil, p[i].x, p[i].px, p[i].s.x0, p[i].s.y0, p[i].s.x1, p[i].s.y1, v,
			p[i].focused ? "*" : "");
	}
}

/* up to max space-separated numbers; the count, -1 on junk */
static int
nums(char *p, int *v, int max)
{
	char *q;
	int n;

	for(n = 0; *p != '\0'; n++, p = q){
		q = strchr(p, ' ');
		q = q == nil ? p + strlen(p) : q;
		if(n == max)
			return -1;
		if(*q == ' ')
			*q++ = '\0';
		if(action_parsenum(p, &v[n]) < 0)
			return -1;
	}
	return n;
}

/* "verb a [b]": a and b, -1 when absent or bad */
static void
meta2(char *line, int *a, int *b)
{
	int v[2], n;

	n = nums(strchr(line, ' ') + 1, v, 2);
	*a = n >= 1 ? v[0] : -1;
	*b = n == 2 ? v[1] : -1;
}

static void
dumpall(Desk *d)
{
	Place p[MAXWIN];
	int i, n;
	char *v;

	n = desk_place(d, p);
	lprint("desk cur %d nws %d nwin %d\n", d->cur, d->nws, d->nwin);
	for(i = 0; i < n; i++){
		v = p[i].vis == VisFull ? "full" : p[i].vis == VisPart ? "part" :
			p[i].vis == VisParked ? "parked" : "off";
		/* layer letters as wintest's place: t tile, f float, F fullscreen */
		lprint("place %d %s %s scr %d %d %d %d%s\n", p[i].id,
			p[i].layer == Ltile ? "t" : p[i].layer == Lfloat ? "f" : "F", v,
			p[i].s.x0, p[i].s.y0, p[i].s.x1, p[i].s.y1, p[i].focused ? " *" : "");
	}
	lprint("hidden");
	for(i = 0; i < d->nhid; i++)
		lprint(" %d", d->hid[i]);
	lprint("\n");
}

static void
cmd(char *line)
{
	char err[128];
	Strip *s;
	LRect r;
	int a, b, i, mode, v[5];

	if(strncmp(line, "size ", 5) == 0){
		meta2(line, &a, &b);
		if(a <= 0 || b <= 0){
			lprint("error bad argument: %s\n", line + 5);
			return;
		}
		if(!sized){
			desk_init(&d, a, b, d.gap);
			sized = 1;
		}else
			desk_resize(&d, a, b);
	}else if(strncmp(line, "gap ", 4) == 0){
		if(action_parsenum(line + 4, &a) < 0 || a < 0)
			lprint("error bad argument: %s\n", line + 4);
		else{
			d.gap = a;
			for(i = 0; i < d.nws; i++)
				strip_setgap(&d.ws[i].st, a);
		}
	}else if(strncmp(line, "center ", 7) == 0){
		if(action_parsecenter(line + 7, &mode) < 0)
			lprint("error bad argument: %s\n", line + 7);
		else
			desk_setcenter(&d, mode);
	}else if(strcmp(line, "add") == 0 || strncmp(line, "add ", 4) == 0){
		a = DEFWIDTH;
		if(line[3] == ' ')
			meta2(line, &a, &b);
		if(a < 0)
			lprint("error bad argument: %s\n", line + 4);
		else if(desk_add(&d, a) < 0)
			lprint("error strip full\n");
	}else if(strcmp(line, "close") == 0){
		desk_remove(&d, desk_focused(&d));
	}else if(strncmp(line, "closeid ", 8) == 0){
		meta2(line, &a, &b);
		if(a < 0)
			lprint("error bad argument: %s\n", line + 8);
		else if(desk_remove(&d, a) < 0)
			lprint("error no such id: %d\n", a);
	}else if(strncmp(line, "focusid ", 8) == 0){
		meta2(line, &a, &b);
		if(a < 0)
			lprint("error bad argument: %s\n", line + 8);
		else
			desk_focus(&d, a);
	}else if(strncmp(line, "widthid ", 8) == 0 || strncmp(line, "widthpx ", 8) == 0){
		meta2(line, &a, &b);
		s = desk_stripof(&d, a);
		if(a < 0 || b < 0)
			lprint("error bad argument: %s\n", line + 8);
		else if(s != nil)
			strip_setwidthid(s, a, line[5] == 'p' ? strip_pxtomil(s, b) : b);
	}else if(strncmp(line, "heightpx ", 9) == 0){
		meta2(line, &a, &b);
		s = desk_stripof(&d, a);
		if(a < 0 || b < 0)
			lprint("error bad argument: %s\n", line + 9);
		else if(s != nil)
			strip_setheightpx(s, a, b);
	}else if(strncmp(line, "float ", 6) == 0){
		if(nums(line + 6, v, 5) != 5 || v[0] < 0)
			lprint("error bad argument: %s\n", line + 6);
		else if(!desk_isfloat(&d, v[0]))
			lprint("error no such id: %d\n", v[0]);
		else{
			r.x0 = v[1];
			r.y0 = v[2];
			r.x1 = v[3];
			r.y1 = v[4];
			desk_floatset(&d, v[0], r);
		}
	}else if(strncmp(line, "raise ", 6) == 0 || strncmp(line, "lower ", 6) == 0){
		meta2(line, &a, &b);
		if(a < 0)
			lprint("error bad argument: %s\n", line + 6);
		else if(!desk_isfloat(&d, a))
			lprint("error no such id: %d\n", a);
		else if(line[0] == 'r')
			desk_raise(&d, a);
		else
			desk_lower(&d, a);
	}else if(strncmp(line, "hide ", 5) == 0 || strncmp(line, "unhide ", 7) == 0){
		meta2(line, &a, &b);
		if(a < 0)
			lprint("error bad argument: %s\n", line + 5);
		else if(line[0] == 'u')
			desk_unhide(&d, a);
		else
			desk_hide(&d, a);
	}else if(strncmp(line, "wsmove ", 7) == 0){
		if(nums(line + 7, v, 2) != 2 || v[0] < 0)
			lprint("error bad argument: %s\n", line + 7);
		else
			desk_movewinws_id(&d, v[0], v[1] - 1);
	}else if(strncmp(line, "wsof ", 5) == 0){
		meta2(line, &a, &b);
		if(a < 0)
			lprint("error bad argument: %s\n", line + 5);
		else
			lprint("wsof %d %d\n", a, desk_wsof(&d, a) + 1);
	}else if(strcmp(line, "who") == 0){
		lprint("focused %d layer %s\n", desk_focused(&d), d.ws[d.cur].layer == Lfloat ? "float" : "tile");
	}else if(strcmp(line, "dump") == 0){
		dump(desk_cur(&d));
	}else if(strcmp(line, "dumpall") == 0){
		dumpall(&d);
	}else if(action_run(&d, line, err, sizeof err) < 0)
		lprint("error %s\n", err);
}

static int
run(void)
{
	desk_init(&d, 1000, 600, 4);
	return script(256, cmd);
}
