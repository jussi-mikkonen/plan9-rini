#include "compat.h"
#include "wq.h"

void
wq_mouseopen(Wq *q, int on)
{
	q->mouseopen = on;
	q->resized = 0;
}

void
wq_kbdopen(Wq *q, int on)
{
	q->kbdopen = on;
	if(!on)
		q->kq.n = 0;
}

void
wq_mouse(Wq *q, int x, int y, int buttons, long msec)
{
	Wmouse m;

	if(!q->mouseopen)
		return;
	m.x = x - q->dx;
	m.y = y - q->dy;
	m.buttons = buttons;
	m.msec = msec;
	m.counter = ++q->counter;
	if(buttons != q->lastb && q->mqn < MOUSEQ)
		q->mq[(q->mqh + q->mqn++) % MOUSEQ] = m;
	q->lastb = buttons;
	q->cur = m;
}

int
wq_mouseready(Wq *q)
{
	return q->mouseopen && q->counter != q->lastcounter;
}

int
wq_mouseread(Wq *q, char *buf)
{
	Wmouse m;

	if(q->mqn > 0){
		m = q->mq[q->mqh];
		q->mqh = (q->mqh + 1) % MOUSEQ;
		q->mqn--;
	}else
		m = q->cur;
	lsnprint(buf, MOUSELEN + 1, "%c%11d %11d %11d %11ld ", q->resized ? 'r' : 'm', m.x, m.y, m.buttons, m.msec);
	q->resized = 0;
	q->lastcounter = m.counter;
	return MOUSELEN;
}

/* like rio's xfidwrite; a number beyond 9 digits is rejected */
int
wq_warp(Wq *q, char *data, int n, int *x, int *y)
{
	char b[64], *p, *e;
	long a, c;

	if(n < 1 || n >= (int)sizeof b || data[0] != 'm')
		return -1;
	memmove(b, data, n);
	b[n] = '\0';
	a = strtol(b + 1, &p, 10);
	if(p == b + 1)
		return -1;
	c = strtol(p, &e, 10);
	if(e == p || a < -999999999 || a > 999999999 || c < -999999999 || c > 999999999)
		return -1;
	*x = a + q->dx;
	*y = c + q->dy;
	return 0;
}

void
wq_moved(Wq *q, int dx, int dy)
{
	q->dx = dx;
	q->dy = dy;
}

void
wq_reshaped(Wq *q)
{
	q->resized = 1;
	q->cur.buttons = 0;
	q->cur.counter = ++q->counter;
	q->wctlready = 1;
}

void
wq_unfocus(Wq *q)
{
	wq_kbd(q, "K", 1);	/* in order: keys pressed after it come after it */
	if(q->cur.buttons){
		q->cur.buttons = 0;
		q->cur.counter = ++q->counter;
	}
	q->wctlready = 1;
}

void
wq_kbd(Wq *q, char *msg, int len)
{
	if(q->kbdopen)
		kq_put(&q->kq, msg, len);
}

int
wq_kbdready(Wq *q)
{
	return q->kq.n > 0;	/* closing empties it */
}

int
wq_kbdread(Wq *q, char *buf, int n)
{
	int tot, k;

	tot = 0;
	while((k = kq_get(&q->kq, buf + tot, n - tot)) > 0)
		tot += k;
	return tot;
}

int
place_hit(Place *p, int n, int x, int y)
{
	int i, hit;

	hit = -1;
	for(i = 0; i < n; i++){
		if(p[i].vis == VisParked || p[i].s.x1 <= p[i].s.x0 || p[i].s.y1 <= p[i].s.y0)
			continue;
		if(x >= p[i].s.x0 && x < p[i].s.x1 && y >= p[i].s.y0 && y < p[i].s.y1)
			hit = i;
	}
	return hit;
}

void
mroute_reset(Mroute *m, int buttons)
{
	m->grab = 0;
	m->swallow = buttons != 0;
	m->lastb = buttons;
}

void
mroute(Mroute *m, Place *p, int n, int focus, int x, int y, int buttons, Mdec *d)
{
	int i, hit, press;

	d->to = 0;
	d->focus = 0;
	if(m->swallow){
		if(buttons == 0)
			m->swallow = 0;
		m->lastb = buttons;
		return;
	}
	if(m->grab){
		d->to = m->grab;
		if(buttons == 0)
			m->grab = 0;
		m->lastb = buttons;
		return;
	}
	i = place_hit(p, n, x, y);
	hit = i < 0 ? 0 : p[i].id;
	press = m->lastb == 0 && buttons != 0;
	if(press && hit != 0 && hit != focus && (buttons & 7)){
		d->focus = hit;
		if(buttons & 1)
			m->swallow = 1;
		else{
			d->to = hit;
			m->grab = hit;
		}
	}else if(press && hit != 0 && hit == focus){
		d->to = hit;
		if(buttons & 7)
			m->grab = hit;
	}else if(hit == focus)
		d->to = focus;
	m->lastb = buttons;
}

enum { Pidle, Ppend, Pdrag, Pswal };

enum {
	Bleft = 1,
	Bright = 4,
	Bwheel = 8|16,
	Cooldown = 150,	/* ms between workspace wheel steps (niri cooldown-ms) */
	Dragpx = 4	/* Mod+B3 on a float: more motion than this is a resize */
};

/*
 * Mouse policy. Press edges only (lastb == 0), like mroute.
 *  plain B3: background, gap or a window's border band -> Dmenu (id 0 or
 *	the window); inside a client -> Dpass; a
 *	fullscreen window has no border band, the client owns every pixel
 *  Mod+B3: tile or background -> Dmenu; float -> pending: motion past
 *	Dragpx -> Dfresize (nearest corner), release first -> Dswallow: no
 *	menu, menuhit needs B3 held
 *  Mod+B1 on a float: Dfmove with the offset from the press
 *  other Mod+B1/B2: Dswallow (rini.c focuses the reported id)
 *  Mod+wheel: Dwheel Wws, 150 ms cooldown; Mod+Shift+wheel: Dwheel Wcol;
 *	the release is swallowed; a plain wheel is Dpass
 * After any gesture everything is Dswallow until all buttons are up.
 */
void
mpol(Mpol *o, Place *p, int n, int x, int y, int buttons, long msec, int mod, int shift, Mact *a)
{
	Place *w;
	int i, press, b, cx, cy;

	memset(a, 0, sizeof *a);
	a->kind = Dpass;
	press = o->lastb == 0 && buttons != 0;
	o->lastb = buttons;
	switch(o->state){
	case Pswal:
		if(buttons == 0)
			o->state = Pidle;
		a->kind = Dswallow;
		return;
	case Ppend:
		a->id = o->id;
		a->dx = x - o->px;
		a->dy = y - o->py;
		if(!(buttons & Bright)){	/* released unmoved: no menu, menuhit needs the button held */
			o->state = buttons ? Pswal : Pidle;
			a->kind = Dswallow;
		}else if(a->dx > Dragpx || a->dx < -Dragpx || a->dy > Dragpx || a->dy < -Dragpx){
			o->state = Pswal;
			a->kind = Dfresize;
			for(i = 0; i < n; i++)
				if(p[i].id == o->id){
					w = &p[i];
					cx = (w->s.x0 + w->s.x1) / 2;
					cy = (w->s.y0 + w->s.y1) / 2;
					a->corner = (o->px >= cx) | (o->py >= cy) << 1;
				}
		}else
			a->kind = Dswallow;
		return;
	case Pdrag:
		a->id = o->id;
		a->dx = x - o->px;
		a->dy = y - o->py;
		a->kind = Dfmove;
		if(!(buttons & Bleft))
			o->state = buttons ? Pswal : Pidle;
		return;
	}
	if(!press)
		return;
	if(mod && (buttons & Bwheel)){
		o->state = Pswal;
		a->kind = Dswallow;
		if(!shift && msec - o->lastwheel < Cooldown)
			return;
		a->kind = Dwheel;
		a->id = shift ? Wcol : Wws;
		a->dir = buttons & 8 ? -1 : 1;
		if(!shift)
			o->lastwheel = msec;
		return;
	}
	i = place_hit(p, n, x, y);
	a->id = i < 0 ? 0 : p[i].id;
	b = i >= 0 && p[i].layer == Lfloat;
	if(buttons & Bright){
		if(mod && b){
			o->state = Ppend;
			o->id = a->id;
			o->px = x;
			o->py = y;
			a->kind = Dswallow;
		}else if(mod || i < 0 || (p[i].layer != Lfs && (x < p[i].s.x0 + o->border
		|| x >= p[i].s.x1 - o->border || y < p[i].s.y0 + o->border || y >= p[i].s.y1 - o->border))){
			o->state = Pswal;
			a->kind = Dmenu;
		}
	}else if(mod && (buttons & Bleft) && b){
		o->state = Pdrag;
		o->id = a->id;
		o->px = x;
		o->py = y;
		a->kind = Dfmove;
	}else if(mod && (buttons & 3)){
		o->state = Pswal;
		a->kind = Dswallow;
	}
}

void
mpol_reset(Mpol *o, int buttons)
{
	o->state = buttons ? Pswal : Pidle;
	o->lastb = buttons;
}
