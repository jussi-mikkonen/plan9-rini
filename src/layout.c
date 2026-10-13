/*
 * layout.c: pure niri-style scrolling column layout. Rules, all integer:
 *
 * Column pixel width: px = (vw - gap) * wmil / 1000 - gap, integer
 * division, minimum 1. Two 500 columns plus three gaps fill vw exactly.
 * Strip-space x: col0.x = gap; col(i+1).x = col(i).x + col(i).px + gap.
 * Screen rect: s.x0 = x - viewx, s.x1 = s.x0 + px, s.y0 = gap,
 * s.y1 = vh - gap (half-open) for a one-window column.
 *
 * Stack height rule: a column holds n windows top to bottom. avail =
 * vh - (n+1)*gap (clamped 0..100000); weight w = hw or 1000 when hw is 0
 * (setshare keeps hw <= HWMAX); height = avail * w / sum(w), the last window takes
 * avail minus the others; any height below MINHPX is raised to MINHPX
 * by taking from the tallest window (only when avail >= n*MINHPX).
 * Window 0 has y0 = gap, each next y0 = previous y1 + gap, so n = 1
 * gives y0 = gap, y1 = vh - gap exactly.
 * Visibility: VisOff if s.x1 <= 0 or s.x0 >= vw; VisFull if s.x0 >= 0
 * and s.x1 <= vw; otherwise VisPart.
 *
 * Follow, for focused column f with L = f.x - gap and R = f.x + f.px + gap:
 *  CenterNever: if L < viewx then viewx = L; else if R > viewx + vw then
 *   viewx = R - vw; else unchanged (minimal scroll to the nearest edge).
 *  CenterAlways: viewx = f.x + f.px/2 - vw/2. No clamping, so the first
 *   and last columns may sit in the middle of the screen.
 *  CenterOverflow: with a valid prevfocus p != focus, span =
 *   max(R, Rp) - min(L, Lp); if span <= vw apply the CenterNever rule,
 *   else the CenterAlways rule. Without a valid prevfocus, CenterNever.
 *
 * Pixel width to permille (wctl): wmil = ((px + gap) * 1000 +
 * (vw - gap) / 2) / (vw - gap), clamped to MINWIDTH..MAXWIDTH; the
 * rounded inverse of the column pixel width.
 *
 * Closing by id: when the focused column goes, the focus moves to the
 * column on its left, else to the new first; when any other column goes,
 * focus stays on the same id.
 *
 * Height model (share = permille of avail): setshare solves the active
 * window's weight as share * S_others / (1000 - share) over effective
 * weights (0 -> 1000), then scales all weights down if one exceeds
 * HWMAX; share is clamped to 50..950 and so every window keeps MINHPX.
 * Presets cycle the share 333, 500, 667 (next above the rounded current
 * share, wrapping); reset zeroes every weight (equal split); adjust is
 * setshare(current + d). All are no-ops for a one-window column.
 *
 * Maximize: wmil becomes MAXWIDTH and the old width is saved in savew;
 * again restores it. Any width setter clears savew.
 *
 * Consume takes the top window of the right column to the bottom of the
 * focused one. Expel moves the active window into a new column beside
 * it with the same width. Move swaps columns or windows, focus follows.
 *
 * Closing a window: the active window of its stack moves to the window
 * above, else below; the last window removes the column.
 *
 * Adding a column inserts it right of the focus at the given width
 * (clamped) and never changes another column's wmil. Resize keeps every wmil (widths scale
 * with the viewport) and then follows focus. Empty strip: viewx = 0.
 */
#include "compat.h"
#include "layout.h"

static int presets[NPRESET] = { 333, 500, 667 };

static int
clampw(int wmil)
{
	if(wmil < MINWIDTH)
		return MINWIDTH;
	if(wmil > MAXWIDTH)
		return MAXWIDTH;
	return wmil;
}

static int
strip_colpx(Strip *s, int wmil)
{
	int px;

	px = (s->vw - s->gap) * wmil / 1000 - s->gap;
	if(px < 1)
		px = 1;
	return px;
}

/* strip-space left edge of column i */
static int
colx(Strip *s, int i)
{
	int j, x;

	x = s->gap;
	for(j = 0; j < i; j++)
		x += strip_colpx(s, s->c[j].wmil) + s->gap;
	return x;
}

static void
followrule(Strip *s, int L, int R)
{
	if(L < s->viewx)
		s->viewx = L;
	else if(R > s->viewx + s->vw)
		s->viewx = R - s->vw;
}

static void
centerrule(Strip *s, int x, int px)
{
	s->viewx = x + px/2 - s->vw/2;
}

void
strip_follow(Strip *s)
{
	int f, p, x, px, xp, L, R, Lp, Rp, lo, hi;

	if(s->n == 0){
		s->viewx = 0;
		return;
	}
	f = s->focus;
	x = colx(s, f);
	px = strip_colpx(s, s->c[f].wmil);
	L = x - s->gap;
	R = x + px + s->gap;
	switch(s->center){
	case CenterAlways:
		centerrule(s, x, px);
		break;
	case CenterOverflow:
		p = s->prevfocus;
		if(p >= 0 && p != f){
			xp = colx(s, p);
			Lp = xp - s->gap;
			Rp = xp + strip_colpx(s, s->c[p].wmil) + s->gap;
			hi = R > Rp ? R : Rp;
			lo = L < Lp ? L : Lp;
			if(hi - lo > s->vw){
				centerrule(s, x, px);
				break;
			}
		}
		followrule(s, L, R);
		break;
	default:
		followrule(s, L, R);
		break;
	}
}

void
strip_init(Strip *s, int vw, int vh, int gap)
{
	s->n = 0;
	s->focus = -1;
	s->prevfocus = -1;
	s->viewx = 0;
	s->vw = vw;
	s->vh = vh;
	s->gap = gap;
	s->center = CenterNever;
}

static void
onecol(Col *c, int id, int wmil)
{
	c->n = 1;
	c->win[0] = id;
	c->hw[0] = 0;
	c->wmil = clampw(wmil);
	c->savew = 0;
	c->active = 0;
}

static void
insat(Strip *s, int at, Col *c)
{
	memmove(&s->c[at + 1], &s->c[at], (s->n - at) * sizeof(Col));
	s->c[at] = *c;
	s->n++;
}

/* whole column right of the focus, focused */
int
strip_inscol(Strip *s, Col *c)
{
	int at;

	if(s->n >= MAXCOL)
		return -1;
	at = s->focus + 1;
	insat(s, at, c);
	s->prevfocus = s->focus;
	s->focus = at;
	strip_follow(s);
	return at;
}

int
strip_add(Strip *s, int id, int wmil)
{
	Col c;

	onecol(&c, id, wmil);
	return strip_inscol(s, &c);
}

static void
delcol(Strip *s, int at)
{
	memmove(&s->c[at], &s->c[at + 1], (s->n - at - 1) * sizeof(Col));
	s->n--;
}

/* remove window wi; active moves to the window above, else stays 0 */
static void
delwin(Col *c, int wi)
{
	memmove(&c->win[wi], &c->win[wi + 1], (c->n - wi - 1) * sizeof(int));
	memmove(&c->hw[wi], &c->hw[wi + 1], (c->n - wi - 1) * sizeof(int));
	c->n--;
	if(wi <= c->active && c->active > 0)
		c->active--;
}

static int
findwin(Strip *s, int id, int *wi)
{
	int i, j;

	for(i = 0; i < s->n; i++)
		for(j = 0; j < s->c[i].n; j++)
			if(s->c[i].win[j] == id){
				*wi = j;
				return i;
			}
	return -1;
}

int
strip_idx(Strip *s, int id)
{
	int wi;

	return findwin(s, id, &wi);
}

int
strip_focused(Strip *s)
{
	Col *c;

	if(s->n == 0)
		return 0;
	c = &s->c[s->focus];
	return c->win[c->active];
}

int
strip_closeid(Strip *s, int id)
{
	int idx, wi;

	idx = findwin(s, id, &wi);
	if(idx < 0)
		return -1;
	delwin(&s->c[idx], wi);
	if(s->c[idx].n == 0){
		delcol(s, idx);
		if(s->n == 0)
			s->focus = -1;
		else if(idx == s->focus)
			s->focus = idx > 0 ? idx - 1 : 0;
		else if(idx < s->focus)
			s->focus--;
		s->prevfocus = -1;
	}
	strip_follow(s);
	return id;
}

void
strip_focusid(Strip *s, int id)
{
	int idx, wi;

	idx = findwin(s, id, &wi);
	if(idx >= 0){
		strip_focusidx(s, idx);
		s->c[idx].active = wi;
	}
}

void
strip_focusidx(Strip *s, int idx)
{
	if(s->n == 0)
		return;
	if(idx < 0)
		idx = 0;
	if(idx > s->n - 1)
		idx = s->n - 1;
	s->prevfocus = s->focus;
	s->focus = idx;
	strip_follow(s);
}

void
strip_focus(Strip *s, int dir)
{
	strip_focusidx(s, s->focus + dir);
}

static void
setwidthidx(Strip *s, int idx, int wmil)
{
	s->c[idx].wmil = clampw(wmil);
	s->c[idx].savew = 0;
	strip_follow(s);
}

void
strip_setwidth(Strip *s, int wmil)
{
	if(s->n == 0)
		return;
	setwidthidx(s, s->focus, wmil);
}

void
strip_setwidthid(Strip *s, int id, int wmil)
{
	int idx;

	idx = strip_idx(s, id);
	if(idx >= 0)
		setwidthidx(s, idx, wmil);
}

int
strip_pxtomil(Strip *s, int px)
{
	return clampw(((px + s->gap) * 1000 + (s->vw - s->gap) / 2) / (s->vw - s->gap));
}

void
strip_adjust(Strip *s, int dmil)
{
	if(s->n == 0)
		return;
	strip_setwidth(s, s->c[s->focus].wmil + dmil);
}

/* the first preset above v, else the first */
static int
nextpreset(int v)
{
	int i;

	for(i = 0; i < NPRESET; i++)
		if(presets[i] > v)
			return presets[i];
	return presets[0];
}

void
strip_preset(Strip *s, int dir)
{
	int i, w, nw;

	if(s->n == 0)
		return;
	w = s->c[s->focus].wmil;
	if(dir > 0)
		nw = nextpreset(w);
	else{
		nw = presets[NPRESET - 1];
		for(i = NPRESET - 1; i >= 0; i--)
			if(presets[i] < w){
				nw = presets[i];
				break;
			}
	}
	strip_setwidth(s, nw);
}

void
strip_setgap(Strip *s, int gap)
{
	s->gap = gap;
	strip_follow(s);
}

static int
effw(Col *c, int i)
{
	return c->hw[i] > 0 ? c->hw[i] : 1000;
}

static int
availh(Strip *s, Col *c)
{
	int avail;

	avail = s->vh - (c->n + 1)*s->gap;
	if(avail < 0)
		return 0;
	if(avail > 100000)
		return 100000;
	return avail;
}

/* heights of the n windows of c into h, per the stack height rule */
static void
colheights(Strip *s, Col *c, int *h)
{
	int i, avail, sum, w[MAXSTACK], lo, hi, mv;

	avail = availh(s, c);
	sum = 0;
	for(i = 0; i < c->n; i++){
		w[i] = effw(c, i);
		sum += w[i];
	}
	mv = avail;
	for(i = 0; i < c->n - 1; i++){
		h[i] = avail * w[i] / sum;
		mv -= h[i];
	}
	h[c->n - 1] = mv;
	if(avail < c->n * MINHPX)
		return;
	for(;;){
		lo = 0;
		hi = 0;
		for(i = 1; i < c->n; i++){
			if(h[i] < h[lo])
				lo = i;
			if(h[i] > h[hi])
				hi = i;
		}
		mv = MINHPX - h[lo];
		if(mv > h[hi] - MINHPX)
			mv = h[hi] - MINHPX;
		if(h[lo] >= MINHPX || mv <= 0)
			break;
		h[lo] += mv;
		h[hi] -= mv;
	}
}

int
strip_place(Strip *s, Place *p)
{
	int i, j, k, x, y, h[MAXSTACK];
	Col *c;

	x = s->gap;
	k = 0;
	for(i = 0; i < s->n; i++){
		c = &s->c[i];
		colheights(s, c, h);
		y = s->gap;
		for(j = 0; j < c->n; j++, k++){
			p[k].id = c->win[j];
			p[k].layer = Ltile;
			p[k].col = i;
			p[k].wmil = c->wmil;
			p[k].x = x;
			p[k].px = strip_colpx(s, c->wmil);
			p[k].s.x0 = x - s->viewx;
			p[k].s.x1 = p[k].s.x0 + p[k].px;
			p[k].s.y0 = y;
			p[k].s.y1 = y + h[j];
			y += h[j] + s->gap;
			if(p[k].s.x1 <= 0 || p[k].s.x0 >= s->vw)
				p[k].vis = VisOff;
			else if(p[k].s.x0 >= 0 && p[k].s.x1 <= s->vw)
				p[k].vis = VisFull;
			else
				p[k].vis = VisPart;
			p[k].focused = i == s->focus && j == c->active;
		}
		x += strip_colpx(s, c->wmil) + s->gap;
	}
	return k;
}

void
strip_resize(Strip *s, int vw, int vh)
{
	s->vw = vw;
	s->vh = vh;
	strip_follow(s);
}

void
strip_center(Strip *s)
{
	if(s->n == 0)
		return;
	centerrule(s, colx(s, s->focus), strip_colpx(s, s->c[s->focus].wmil));
}

int
strip_consume(Strip *s)
{
	Col *c, *r;

	if(s->n == 0 || s->focus + 1 >= s->n)
		return 0;
	c = &s->c[s->focus];
	r = &s->c[s->focus + 1];
	if(c->n >= MAXSTACK)
		return 0;
	c->win[c->n] = r->win[0];
	c->hw[c->n] = 0;
	c->n++;
	delwin(r, 0);
	if(r->n == 0){
		delcol(s, s->focus + 1);
		s->prevfocus = -1;
	}
	strip_follow(s);
	return 1;
}

/* active window of the focused stack into a new column at focus+1 (dir > 0) or focus */
static int
expel(Strip *s, int dir)
{
	Col *c, nc;
	int at;

	if(s->n == 0 || s->n >= MAXCOL)
		return 0;
	c = &s->c[s->focus];
	if(c->n < 2)
		return 0;
	at = dir > 0 ? s->focus + 1 : s->focus;
	onecol(&nc, c->win[c->active], c->wmil);
	nc.savew = c->savew;
	delwin(c, c->active);
	insat(s, at, &nc);
	s->prevfocus = dir > 0 ? s->focus : s->focus + 1;
	s->focus = at;
	strip_follow(s);
	return 1;
}

int
strip_expel(Strip *s)
{
	return expel(s, 1);
}

int
strip_consumeorexpel(Strip *s, int dir)
{
	Col *c, *nb;
	int at;

	if(s->n == 0)
		return 0;
	c = &s->c[s->focus];
	if(c->n > 1)
		return expel(s, dir);
	at = s->focus + dir;
	if(at < 0 || at >= s->n || s->c[at].n >= MAXSTACK)
		return 0;
	nb = &s->c[at];
	nb->win[nb->n] = c->win[0];
	nb->hw[nb->n] = 0;
	nb->active = nb->n;
	nb->n++;
	delcol(s, s->focus);
	s->focus = dir > 0 ? s->focus : at;
	s->prevfocus = -1;
	strip_follow(s);
	return 1;
}

int
strip_focuswin(Strip *s, int dir)
{
	Col *c;
	int a;

	if(s->n == 0)
		return 1;
	c = &s->c[s->focus];
	a = c->active + dir;
	if(a < 0 || a >= c->n)
		return 1;
	c->active = a;
	return 0;
}

void
strip_movecol(Strip *s, int dir)
{
	Col t;
	int j;

	j = s->focus + dir;
	if(s->n == 0 || j < 0 || j >= s->n)
		return;
	t = s->c[j];
	s->c[j] = s->c[s->focus];
	s->c[s->focus] = t;
	s->prevfocus = s->focus;
	s->focus = j;
	strip_follow(s);
}

int
strip_movewin(Strip *s, int dir)
{
	Col *c;
	int a, b, t;

	if(s->n == 0)
		return 1;
	c = &s->c[s->focus];
	a = c->active;
	b = a + dir;
	if(b < 0 || b >= c->n)
		return 1;
	t = c->win[a];
	c->win[a] = c->win[b];
	c->win[b] = t;
	t = c->hw[a];
	c->hw[a] = c->hw[b];
	c->hw[b] = t;
	c->active = b;
	return 0;
}

/* current permille share of window wi, rounded */
static int
shareof(Col *c, int wi)
{
	int i, sum;

	sum = 0;
	for(i = 0; i < c->n; i++)
		sum += effw(c, i);
	return (effw(c, wi) * 1000 + sum/2) / sum;
}

static void
setshareat(Strip *s, Col *c, int wi, int share)
{
	int i, avail, lo, hi, so, wa;

	avail = availh(s, c);
	if(c->n < 2 || avail < c->n*MINHPX)	/* no room for MINHPX each: lo > hi */
		return;
	lo = (MINHPX*1000 + avail - 1) / avail;
	hi = 1000 - (c->n - 1)*MINHPX*1000 / avail;
	if(lo < 50)
		lo = 50;
	if(hi > 950)
		hi = 950;
	if(share < lo)
		share = lo;
	if(share > hi)
		share = hi;
	so = 0;
	for(i = 0; i < c->n; i++){
		c->hw[i] = effw(c, i);
		if(i != wi)
			so += c->hw[i];
	}
	wa = share * so / (1000 - share);
	if(wa < 1)
		wa = 1;
	if(wa > HWMAX){
		for(i = 0; i < c->n; i++)
			if(i != wi){
				c->hw[i] = c->hw[i] * HWMAX / wa;
				if(c->hw[i] < 1)
					c->hw[i] = 1;
			}
		wa = HWMAX;
	}
	c->hw[wi] = wa;
}

void
strip_setshare(Strip *s, int share)
{
	Col *c;

	if(s->n == 0)
		return;
	c = &s->c[s->focus];
	setshareat(s, c, c->active, share);
}

void
strip_adjshare(Strip *s, int dshare)
{
	Col *c;

	if(s->n == 0)
		return;
	c = &s->c[s->focus];
	setshareat(s, c, c->active, shareof(c, c->active) + dshare);
}

void
strip_presetheight(Strip *s)
{
	Col *c;

	if(s->n == 0)
		return;
	c = &s->c[s->focus];
	setshareat(s, c, c->active, nextpreset(shareof(c, c->active)));
}

void
strip_resetheight(Strip *s)
{
	Col *c;
	int i;

	if(s->n == 0)
		return;
	c = &s->c[s->focus];
	for(i = 0; i < c->n; i++)
		c->hw[i] = 0;
}

void
strip_setheightpx(Strip *s, int id, int px)
{
	Col *c;
	int idx, wi, avail;

	idx = findwin(s, id, &wi);
	if(idx < 0)
		return;
	c = &s->c[idx];
	avail = availh(s, c);
	if(avail == 0)
		return;
	if(px < 0)
		px = 0;
	if(px > avail)
		px = avail;
	setshareat(s, c, wi, px * 1000 / avail);
}

void
strip_maximize(Strip *s)
{
	Col *c;

	if(s->n == 0)
		return;
	c = &s->c[s->focus];
	if(c->savew != 0){
		c->wmil = c->savew;
		c->savew = 0;
	}else{
		c->savew = c->wmil;
		c->wmil = MAXWIDTH;
	}
	strip_follow(s);
}
