/*
 * ctlfs.c: the files of the ctl tree (/mnt/rini). Runs on the main thread
 * only and answers every Req exactly once.
 */
#include "compat.h"
#include <draw.h>
#include <thread.h>
#include <mouse.h>
#include <cursor.h>
#include <fcall.h>
#include <9p.h>
#include "layout.h"
#include "desk.h"
#include "action.h"
#include "bind.h"
#include "tkeys.h"
#include "ctl.h"
#include "rule.h"
#include "input.h"
#include "ev.h"
#include "wq.h"
#include "wctl.h"
#include "dat.h"
#include "fns.h"

extern Bindtab bt;
extern Tkeytab tk;

static Ruletab rt;	/* starts empty: policy lives in rc */

/* ~35 KB each: static, swapped by index. snaps[cur] is the last commit, valid while evl is non-nil */
static Snap snaps[2];
static int cur;
static Faux *evl;	/* open event readers */

enum { Statebuf = MAXWIN*EVLINE + 256 };

static void
snapbuild(Snap *s)
{
	Win *w;
	Snapwin *sw;
	Strip *st;

	s->n = 0;
	s->nws = desk.nws;
	s->cur = desk.cur;
	s->focus = desk_focused(&desk);
	for(w = wins; w != nil && s->n < MAXWIN; w = w->next){
		if(w->deleted)
			continue;
		sw = &s->w[s->n++];
		sw->id = w->id;
		sw->ws = desk_wsof(&desk, w->id);
		sw->wmil = 0;
		if(desk_ishidden(&desk, w->id))
			sw->kind = Khid;
		else if(desk_isfloat(&desk, w->id))
			sw->kind = Kfloat;
		else{
			sw->kind = Ktile;
			st = desk_stripof(&desk, w->id);
			sw->wmil = st->c[strip_idx(st, w->id)].wmil;
		}
		lsnprint(sw->label, EVLABEL, "%s", w->label);
	}
}

static void
evpush(char *line, void *q)
{
	evq_push(q, line);
}

static void
evemit(char *line, void*)
{
	Faux *a;

	for(a = evl; a != nil; a = a->evnext)
		evq_push(a->evq, line);
}

void
ctlfs_evopen(Faux *a)
{
	a->evq = emalloc9p(sizeof(Evq));
	if(evl == nil)
		snapbuild(&snaps[cur]);
	ev_snapshot(&snaps[cur], evpush, a->evq);
	a->evnext = evl;
	evl = a;
}

void
ctlfs_evclose(Faux *a)
{
	Faux **p;

	for(p = &evl; *p != a; p = &(*p)->evnext)
		;
	*p = a->evnext;
	free(a->evq);
}

/* answers a's parked read when its queue holds something; a may be gone afterwards */
static void
evanswer(Faux *a)
{
	Req *r;
	int n;

	r = a->evpend;
	n = evq_read(a->evq, r->ofcall.data, r->ifcall.count);
	if(n <= 0)
		return;
	a->evpend = nil;
	r->ofcall.count = n;
	respond(r, nil);
}

void
ctlfs_commit(void)
{
	Faux *a, *nx;

	if(evl == nil)
		return;
	snapbuild(&snaps[!cur]);
	ev_diff(&snaps[cur], &snaps[!cur], evemit, nil);
	cur = !cur;
	for(a = evl; a != nil; a = nx){
		nx = a->evnext;
		if(a->evpend != nil)
			evanswer(a);
	}
}

void
ctlfs_read(Req *r)
{
	Faux *a;
	char buf[256], mod[ACTLEN], *s, *p;
	int i;

	a = r->fid->aux;
	switch(a->file){
	case Qevent:	/* never blocks main: one read parks per fid */
		if(a->evpend != nil){
			respond(r, Ebusy);
			return;
		}
		a->evpend = r;
		evanswer(a);
		return;
	case Qstate:	/* the uncommitted snap is scratch; the next commit rebuilds it */
		s = emalloc9p(Statebuf);
		snapbuild(&snaps[!cur]);
		p = s + ev_statefmt(&snaps[!cur], s, Statebuf);
		break;
	case Qctl:
		bind_modfmt(&bt, mod, sizeof mod);
		ctl_summary(buf, sizeof buf, mod, action_centername(desk.center), tk.c64,
			bgcolor, desk_focused(&desk), desk.cur+1);
		s = buf;
		p = s + strlen(s);
		break;
	case Qrules:
		s = p = emalloc9p(MAXRULE*(RULESRC+128));
		for(i = 0; i < rt.n; i++){
			rule_fmt(&rt.r[i], p, RULESRC+127);
			p += strlen(p);
			*p++ = '\n';
		}
		break;
	default:	/* bindings */
		s = p = emalloc9p(MAXBIND*(ACTLEN+64));
		for(i = 0; i < bt.n; i++){
			bind_line(&bt.b[i], p, s + MAXBIND*(ACTLEN+64) - p - 1);
			p += strlen(p);
			*p++ = '\n';
		}
		break;
	}
	readbuf(r, s, p - s);
	if(s != buf)
		free(s);
	respond(r, nil);
}

/*
 * Apply the rules matching subject to w: kind, then rect, width, workspace.
 * No relayout: the main loop commits after every event. The workspace move
 * keeps the current workspace and focus.
 */
void
rules_apply(Win *w, char *subject)
{
	Props p;
	Strip *s;
	LRect r;
	int id;

	id = w->id;
	rule_match(&rt, subject, &p);
	if(p.set == 0 || desk_ishidden(&desk, id))
		return;
	if(((p.set & Pfloat) && !desk_isfloat(&desk, id)) || ((p.set & Ptile) && desk_isfloat(&desk, id)))
		desk_togglefloat(&desk, id);
	if((p.set & Prect) && desk_isfloat(&desk, id)){
		r.x0 = p.rect[0];
		r.y0 = p.rect[1];
		r.x1 = p.rect[2];
		r.y1 = p.rect[3];
		desk_floatset(&desk, id, r);
	}
	s = desk_stripof(&desk, id);
	if((p.set & Pwidth) && s != nil)
		strip_setwidthid(s, id, p.widthpx ? strip_pxtomil(s, p.width) : p.width);
	if(p.set & Pws)
		desk_movewinws_id(&desk, id, p.ws - 1);
}

/* 0 or -1 with err */
static int
ctlexec(Ctlcmd *c, char *err, int nerr)
{
	char buf[CTLLINE+8];
	int mode;

	switch(c->verb){
	case Cbind:
		return bind_add(&bt, c->arg[0], c->arg[1], c->rest, err, nerr);
	case Cunbind:
		return bind_del(&bt, c->arg[0], err, nerr);
	case Ctermbind:
		return tkeys_add(&tk, c->arg[0], c->arg[1], err, nerr);
	case Cset:
		switch(c->set){
		case Smod:
			return bind_setmod(&bt, c->arg[0], err, nerr);
		case Scenter:
			if(action_parsecenter(c->arg[0], &mode) < 0){
				snprint(err, nerr, "bad centring mode: %s", c->arg[0]);
				return -1;
			}
			desk_setcenter(&desk, mode);
			return 0;
		case Sc64return:
			tk.c64 = strcmp(c->arg[0], "on") == 0;
			return 0;
		case Sbackground:
			if(rescreen(c->colour) < 0){
				snprint(err, nerr, "cannot allocate background");
				return -1;
			}
			return 0;
		}
		break;
	case Crule:
		return rule_add(&rt, c->arg[0], c->arg+1, c->narg-1, err, nerr);
	case Cunrule:
		return rule_del(&rt, c->arg[0], err, nerr);
	case Cspawn:
		snprint(buf, sizeof buf, "spawn %s", c->rest);
		return ui_action(buf, 0, err, nerr);
	case Caction:
		return ui_action(c->rest, 0, err, nerr);
	}
	snprint(err, nerr, "not available yet");
	return -1;
}

/* one or more newline-separated lines; stops at the first failure and names its line when there are several */
void
ctlfs_write(Req *r)
{
	static char msg[ERRMAX];	/* respond keeps the pointer */
	Ctlcmd c;
	Faux *a;
	char *buf, *p, *nl, err[ERRMAX];
	ulong n;
	int line, multi, ok;

	a = r->fid->aux;
	n = r->ifcall.count;
	if(a->file != Qctl){
		respond(r, Eperm);
		return;
	}
	if(n >= 8192){
		respond(r, "write too long");
		return;
	}
	buf = emalloc9p(n + 1);
	memmove(buf, r->ifcall.data, n);
	buf[n] = '\0';
	if(n > 0 && buf[n-1] == '\n')
		buf[n-1] = '\0';
	multi = strchr(buf, '\n') != nil;
	line = 0;
	for(p = buf; p != nil; p = nl){
		line++;
		nl = strchr(p, '\n');
		if(nl != nil)
			*nl++ = '\0';
		if(*p == '\0')
			continue;
		ok = ctl_parse(p, &c, err, sizeof err);
		if(ok == 0 && c.verb == Caction && strcmp(c.rest, "quit") == 0){
			free(buf);
			r->ofcall.count = n;
			respond(r, nil);	/* quit does not return */
			ui_action("quit", 0, err, sizeof err);
			return;
		}
		if(ok == 0)
			ok = ctlexec(&c, err, sizeof err);
		if(ok < 0){
			if(multi)
				snprint(msg, sizeof msg, "line %d: %s", line, err);
			else
				snprint(msg, sizeof msg, "%s", err);
			free(buf);
			respond(r, msg);
			return;
		}
	}
	free(buf);
	r->ofcall.count = n;
	respond(r, nil);
}
