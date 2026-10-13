/*
 * desk.c: pure workspaces. A Desk is a vertical list of workspaces, each
 * a Strip plus floats. Invariants, restored by desk_norm() at the end of every
 * mutator:
 *  1. The last workspace is empty (no tiles, no floats, fs 0), so there
 *     is always one to open a window on; a window added there makes a
 *     new empty one appear below. At MAXWS no new one is made.
 *  2. An empty workspace that is not the last is deleted as soon as the
 *     focus is not on it; positions below shift up by one.
 *  3. layer is Lfloat only while nfl > 0 and the focused float is
 *     fl[nfl-1].
 *  4. A window id lives in exactly one of a column, a float list or hid[].
 *  5. fs is 0 or the focused window of its workspace.
 * Workspace positions are never stored; only cur is an index.
 *
 * Placement, in paint order: current workspace tiles, its fullscreen
 * window, its floats bottom to top, then every other workspace's windows
 * as VisParked at their real sizes, so a switch never resizes. The one
 * exception is a fullscreen window: parked at its tile size, it is
 * resized to the screen when its workspace becomes current.
 */
#include "compat.h"
#include "layout.h"
#include "desk.h"

static int
wsempty(Ws *w)
{
	return w->st.n == 0 && w->nfl == 0 && w->fs == 0;
}

/* the focused window of w: the top float in the float layer, else the tile */
static int
wsfocused(Ws *w)
{
	if(w->layer == Lfloat && w->nfl > 0)
		return w->fl[w->nfl - 1].id;
	return strip_focused(&w->st);
}

static void
newws(Desk *d)
{
	Ws *w;

	w = &d->ws[d->nws++];
	strip_init(&w->st, d->vw, d->vh, d->gap);
	w->st.center = d->center;
	w->nfl = 0;
	w->fs = 0;
	w->layer = Ltile;
}

void
desk_norm(Desk *d)
{
	int i;

	for(i = 0; i < d->nws; i++){
		if(d->ws[i].nfl == 0)
			d->ws[i].layer = Ltile;
		if(d->ws[i].fs != wsfocused(&d->ws[i]))
			d->ws[i].fs = 0;
	}
	for(i = 0; i < d->nws - 1; i++)
		if(i != d->cur && wsempty(&d->ws[i])){
			memmove(&d->ws[i], &d->ws[i + 1], (d->nws - i - 1) * sizeof(Ws));
			d->nws--;
			if(i < d->cur)
				d->cur--;
			i--;
		}
	if(!wsempty(&d->ws[d->nws - 1]) && d->nws < MAXWS)
		newws(d);
}

void
desk_init(Desk *d, int vw, int vh, int gap)
{
	d->nws = 0;
	d->cur = 0;
	d->nhid = 0;
	d->nextid = 1;
	d->nwin = 0;
	d->vw = vw;
	d->vh = vh;
	d->gap = gap;
	d->center = CenterNever;
	newws(d);
}

Strip *
desk_cur(Desk *d)
{
	return &d->ws[d->cur].st;
}

Strip *
desk_stripof(Desk *d, int id)
{
	int i;

	for(i = 0; i < d->nws; i++)
		if(strip_idx(&d->ws[i].st, id) >= 0)
			return &d->ws[i].st;
	return nil;
}

/* the last empty workspace cannot grow another below it */
static int
nogrow(Desk *d, int idx)
{
	return idx == d->nws - 1 && d->nws == MAXWS && wsempty(&d->ws[idx]);
}

int
desk_add(Desk *d, int wmil)
{
	Ws *w;
	int id;

	if(d->nwin >= MAXWIN || nogrow(d, d->cur))
		return -1;
	w = &d->ws[d->cur];
	id = d->nextid;
	if(strip_add(&w->st, id, wmil) < 0)
		return -1;
	d->nextid++;
	d->nwin++;
	w->layer = Ltile;
	desk_norm(d);
	return id;
}

/* the float entry for id, with its workspace */
static Fl *
findfl(Desk *d, int id, Ws **wp)
{
	Ws *w;
	int i, j;

	for(i = 0; i < d->nws; i++){
		w = &d->ws[i];
		for(j = 0; j < w->nfl; j++)
			if(w->fl[j].id == id){
				*wp = w;
				return &w->fl[j];
			}
	}
	return nil;
}

static void
delfl(Ws *w, Fl *f)
{
	memmove(f, f + 1, (w->fl + w->nfl - f - 1) * sizeof(Fl));
	w->nfl--;
}

/* take id out of its column or float list; 1 if found, leaving the cleanup to desk_norm */
static int
detach(Desk *d, int id)
{
	Ws *w;
	Fl *f;
	int i;

	for(i = 0; i < d->nws; i++)
		if(strip_closeid(&d->ws[i].st, id) >= 0)
			return 1;
	f = findfl(d, id, &w);
	if(f == nil)
		return 0;
	delfl(w, f);
	return 1;
}

/* take id out of hid[]; 1 if it was there */
static int
unhid(Desk *d, int id)
{
	int i;

	for(i = 0; i < d->nhid; i++)
		if(d->hid[i] == id){
			memmove(&d->hid[i], &d->hid[i + 1], (d->nhid - i - 1) * sizeof(int));
			d->nhid--;
			return 1;
		}
	return 0;
}

int
desk_remove(Desk *d, int id)
{
	if(!detach(d, id) && !unhid(d, id))
		return -1;
	d->nwin--;
	desk_norm(d);
	return id;
}

int
desk_focused(Desk *d)
{
	return wsfocused(&d->ws[d->cur]);
}

/* idx is a position before any cleanup; desk_norm drops the ws we leave */
void
desk_focusws(Desk *d, int idx)
{
	if(idx < 0 || idx >= d->nws)
		return;
	d->cur = idx;
	desk_norm(d);
}

void
desk_focus(Desk *d, int id)
{
	Ws *w;
	int i;

	i = desk_wsof(d, id);
	if(i < 0)
		return;
	w = &d->ws[i];
	if(desk_isfloat(d, id)){
		desk_raise(d, id);	/* desk_norm there keeps i: no workspace empties */
		w->layer = Lfloat;
	}else{
		strip_focusid(&w->st, id);
		w->layer = Ltile;
	}
	desk_focusws(d, i);
}

/* clip [*lo, *hi) to at most max and shift it into [0, max] */
static void
fit(int *lo, int *hi, int max)
{
	if(*hi - *lo > max)
		*hi = *lo + max;
	if(*hi > max){
		*lo -= *hi - max;
		*hi = max;
	}
	if(*lo < 0){
		*hi -= *lo;
		*lo = 0;
	}
}

/* floats are fixed to the screen: a smaller view pulls them back in */
void
desk_resize(Desk *d, int vw, int vh)
{
	LRect *r;
	int i, j;

	d->vw = vw;
	d->vh = vh;
	for(i = 0; i < d->nws; i++){
		strip_resize(&d->ws[i].st, vw, vh);
		for(j = 0; j < d->ws[i].nfl; j++){
			r = &d->ws[i].fl[j].r;
			fit(&r->x0, &r->x1, vw);
			fit(&r->y0, &r->y1, vh);
		}
	}
}

void
desk_setcenter(Desk *d, int mode)
{
	int i;

	d->center = mode;
	for(i = 0; i < d->nws; i++){
		d->ws[i].st.center = mode;
		strip_follow(&d->ws[i].st);
	}
}

/* the float's visibility against the view */
static int
visof(Desk *d, LRect *r)
{
	if(r->x1 <= 0 || r->x0 >= d->vw || r->y1 <= 0 || r->y0 >= d->vh)
		return VisOff;
	if(r->x0 >= 0 && r->x1 <= d->vw && r->y0 >= 0 && r->y1 <= d->vh)
		return VisFull;
	return VisPart;
}

/* workspace i's floats bottom to top */
static int
placefl(Desk *d, int i, Place *p)
{
	Ws *w;
	int k;

	w = &d->ws[i];
	for(k = 0; k < w->nfl; k++){
		p[k].id = w->fl[k].id;
		p[k].layer = Lfloat;
		p[k].col = -1;
		p[k].wmil = 0;
		p[k].x = 0;
		p[k].px = w->fl[k].r.x1 - w->fl[k].r.x0;
		p[k].s = w->fl[k].r;
		p[k].vis = i == d->cur ? visof(d, &w->fl[k].r) : VisParked;
		p[k].focused = i == d->cur && w->layer == Lfloat && k == w->nfl - 1;
	}
	return w->nfl;
}

int
desk_place(Desk *d, Place *p)
{
	Ws *w;
	Place f;
	int i, j, n;

	w = &d->ws[d->cur];
	n = strip_place(&w->st, p);
	if(w->layer == Lfloat)	/* the focus is a float: no tile looks focused */
		for(i = 0; i < n; i++)
			p[i].focused = 0;
	if(w->fs != 0)
		for(i = 0; i < n; i++)
			if(p[i].id == w->fs){
				f = p[i];
				memmove(&p[i], &p[i + 1], (n - i - 1) * sizeof(Place));
				f.layer = Lfs;
				f.s.x0 = 0;
				f.s.y0 = 0;
				f.s.x1 = d->vw;
				f.s.y1 = d->vh;
				f.vis = VisFull;
				f.focused = 1;
				p[n - 1] = f;
				break;
			}
	n += placefl(d, d->cur, p + n);
	for(i = 0; i < d->nws; i++){
		if(i == d->cur)
			continue;
		j = strip_place(&d->ws[i].st, p + n);
		for(; j > 0; j--, n++){
			p[n].vis = VisParked;
			p[n].focused = 0;
		}
		n += placefl(d, i, p + n);
	}
	return n;
}

/* the focused tile's strip, nil when the target is unusable: idx must be
 * another workspace with room for a column and for a new one below */
static Strip *
movesrc(Desk *d, int idx)
{
	Ws *w;

	w = &d->ws[d->cur];
	if(w->layer != Ltile || w->st.n == 0 || idx < 0 || idx >= d->nws || idx == d->cur)
		return nil;
	if(d->ws[idx].st.n >= MAXCOL || nogrow(d, idx))
		return nil;
	return &w->st;
}

/* the column goes right of the target's focused column; focus follows,
 * and leaving the source lets desk_norm drop it when empty */
void
desk_movecolws(Desk *d, int idx)
{
	Strip *s;
	Col c;
	int i;

	s = movesrc(d, idx);
	if(s == nil)
		return;
	c = s->c[s->focus];
	for(i = 0; i < c.n; i++)
		strip_closeid(s, c.win[i]);
	strip_inscol(&d->ws[idx].st, &c);
	d->ws[idx].layer = Ltile;
	desk_focusws(d, idx);
}

/* the focused window alone, as a new column with the source column's width */
void
desk_movewinws(Desk *d, int idx)
{
	int id;

	if(movesrc(d, idx) == nil)
		return;
	id = desk_focused(d);
	desk_movewinws_id(d, id, idx);
	desk_focus(d, id);
}

/* a tile or float to workspace idx without switching: cur and both
 * workspaces' focus stay, except that a focused window leaving falls back
 * as strip_closeid does. No-op for hidden, unknown or unusable idx. */
void
desk_movewinws_id(Desk *d, int id, int idx)
{
	Ws *src, *dst;
	Fl *f, fl;
	Strip *s;
	int ci, keep, wmil;

	if(idx < 0 || idx >= d->nws)
		return;
	dst = &d->ws[idx];
	f = findfl(d, id, &src);
	if(f != nil){
		if(src == dst || dst->nfl >= MAXFL || nogrow(d, idx))
			return;
		fl = *f;
		delfl(src, f);
		dst->fl[dst->nfl++] = fl;
	}else{
		s = desk_stripof(d, id);
		if(s == nil || s == &dst->st || dst->st.n >= MAXCOL || nogrow(d, idx))
			return;
		ci = strip_idx(s, id);
		wmil = s->c[ci].wmil;
		keep = strip_focused(&dst->st);
		strip_closeid(s, id);
		strip_add(&dst->st, id, wmil);
		if(keep != 0)
			strip_focusid(&dst->st, keep);
	}
	desk_norm(d);
}

int
desk_wsof(Desk *d, int id)
{
	Ws *w;
	int i;

	for(i = 0; i < d->nws; i++)
		if(strip_idx(&d->ws[i].st, id) >= 0)
			return i;
	if(findfl(d, id, &w) != nil)
		return w - d->ws;
	return -1;
}

int
desk_isfloat(Desk *d, int id)
{
	Ws *w;

	return findfl(d, id, &w) != nil;
}

int
desk_floatrect(Desk *d, int id, LRect *r)
{
	Ws *w;
	Fl *f;

	f = findfl(d, id, &w);
	if(f == nil)
		return 0;
	*r = f->r;
	return 1;
}

void
desk_floatset(Desk *d, int id, LRect r)
{
	Ws *w;
	Fl *f;

	f = findfl(d, id, &w);
	if(f != nil)
		f->r = r;
}

/* the window's rect on screen: a float's own, a tile's as laid out */
static int
winrect(Desk *d, int id, LRect *r)
{
	static Place p[MAXPLACE];	/* ~24 KB: never on the 8 KB threadmain stack */
	Strip *s;
	int i, n;

	if(desk_floatrect(d, id, r))
		return 1;
	s = desk_stripof(d, id);
	if(s == nil)
		return 0;
	n = strip_place(s, p);
	for(i = 0; i < n; i++)
		if(p[i].id == id){
			*r = p[i].s;
			return 1;
		}
	return 0;
}

/* a float keeps its tile size (always within the view) and is centred; a tile
 * comes back as a new column right of the focused one with the float's width */
void
desk_togglefloat(Desk *d, int id)
{
	Ws *w;
	Fl *f;
	LRect r;
	int i, wd, ht;

	f = findfl(d, id, &w);
	if(f != nil){
		if(w->st.n >= MAXCOL)
			return;
		wd = f->r.x1 - f->r.x0;
		delfl(w, f);
		strip_add(&w->st, id, strip_pxtomil(&w->st, wd));
		w->layer = Ltile;
		desk_norm(d);
		return;
	}
	i = desk_wsof(d, id);
	if(i < 0 || d->ws[i].nfl == MAXFL || !winrect(d, id, &r))
		return;
	w = &d->ws[i];
	wd = r.x1 - r.x0;
	ht = r.y1 - r.y0;
	r.x0 = (d->vw - wd) / 2;
	r.y0 = (d->vh - ht) / 2;
	r.x1 = r.x0 + wd;
	r.y1 = r.y0 + ht;
	strip_closeid(&w->st, id);
	w->fl[w->nfl].id = id;
	w->fl[w->nfl].r = r;
	w->nfl++;
	w->layer = Lfloat;
	desk_norm(d);
}

void
desk_raise(Desk *d, int id)
{
	Ws *w;
	Fl *f, t;

	f = findfl(d, id, &w);
	if(f == nil)
		return;
	t = *f;
	delfl(w, f);
	w->fl[w->nfl++] = t;
	desk_norm(d);
}

void
desk_lower(Desk *d, int id)
{
	Ws *w;
	Fl *f, t;

	f = findfl(d, id, &w);
	if(f == nil)
		return;
	t = *f;
	memmove(w->fl + 1, w->fl, (f - w->fl) * sizeof(Fl));
	w->fl[0] = t;
	desk_norm(d);
}

/* only when the other layer has something to focus */
void
desk_switchlayer(Desk *d)
{
	Ws *w;

	w = &d->ws[d->cur];
	if(w->layer == Ltile && w->nfl > 0)
		w->layer = Lfloat;
	else if(w->layer == Lfloat && w->st.n > 0)
		w->layer = Ltile;
	desk_norm(d);
}

/* rotate the stack so the next float in dir is on top and focused */
void
desk_floatfocus(Desk *d, int dir)
{
	Ws *w;
	Fl t;

	w = &d->ws[d->cur];
	if(w->layer != Lfloat || w->nfl < 2)
		return;
	if(dir > 0){
		t = w->fl[0];
		memmove(&w->fl[0], &w->fl[1], (w->nfl - 1) * sizeof(Fl));
		w->fl[w->nfl - 1] = t;
	}else{
		t = w->fl[w->nfl - 1];
		memmove(&w->fl[1], &w->fl[0], (w->nfl - 1) * sizeof(Fl));
		w->fl[0] = t;
	}
	desk_norm(d);
}

int
desk_ishidden(Desk *d, int id)
{
	int i;

	for(i = 0; i < d->nhid; i++)
		if(d->hid[i] == id)
			return 1;
	return 0;
}

/* out of the layout into hid[]; the old position is not remembered */
void
desk_hide(Desk *d, int id)
{
	if(d->nhid >= MAXHID || !detach(d, id))
		return;
	d->hid[d->nhid++] = id;
	desk_norm(d);
}

void
desk_unhide(Desk *d, int id)
{
	Ws *w;

	w = &d->ws[d->cur];
	if(w->st.n >= MAXCOL || nogrow(d, d->cur) || !unhid(d, id))
		return;
	strip_add(&w->st, id, DEFWIDTH);
	w->layer = Ltile;
	desk_norm(d);
}

/* toggles the focused tile; desk_norm clears it when the focus moves */
void
desk_fullscreen(Desk *d)
{
	Ws *w;

	w = &d->ws[d->cur];
	if(w->layer != Ltile || w->st.n == 0)
		return;
	w->fs = w->fs != 0 ? 0 : strip_focused(&w->st);
}
