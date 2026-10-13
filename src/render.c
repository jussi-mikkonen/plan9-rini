/*
 * render.c: the only file that creates, names, moves, paints or frees window images.
 *
 * Every client window is a Refbackup window on the private screen (wscreen),
 * named for /dev/winname. A size change reshapes with rio's order (new image,
 * border, new name, swap, old image moved off-screen then freed). Moves and
 * parking are position-only (originwindow), so the client is not told to
 * redraw. Coordinates are clamped to +/-Bound (100000) relative to the
 * viewport. A nil allocwindow is recoverable: the image is parked as a plain
 * image (allocimage) until its next size change. Nothing here is fatal after
 * startup.
 *
 * Place/park/re-top contract: render_sync owns every window image. Visible
 * places are positioned; VisParked places (other workspaces) and windows
 * without a place (hidden) are parked at (Bound, Bound) with their size kept,
 * so a workspace switch is an instant position-only cut. The fullscreen
 * window is the screen outset by Border (the client insets by
 * Borderwidth) and gets no border. allocwindow's z-order is unknown, so
 * after any reshape or change of the fullscreen/float sequence the fullscreen
 * window and then the floats, bottom to top, are topped again.
 */
#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <mouse.h>
#include <cursor.h>
#include <fcall.h>
#include <9p.h>
#include "layout.h"
#include "desk.h"
#include "input.h"
#include "wq.h"
#include "wctl.h"
#include "dat.h"
#include "fns.h"

ulong bgcolor = 0x777777FF;
static Image *bfocus, *bunfocus;
static int fsid;	/* the fullscreen window: no border */
int failafter;		/* test hook (rini -F n): allocations fail from the n-th reshape on; 0 = off */

Image*
solid(ulong rgba)
{
	return allocimage(display, Rect(0, 0, 1, 1), RGB24, 1, rgba);
}

int
render_init(void)
{
	background = solid(bgcolor);
	bfocus = solid(0xFFD700FF);
	bunfocus = solid(0x444444FF);
	if(background == nil || bfocus == nil || bunfocus == nil){
		fprint(2, "render_init: allocimage: %r\n");
		return -1;
	}
	return 0;
}

static int
clamp(int v)
{
	if(v < -Bound)
		return -Bound;
	if(v > Bound)
		return Bound;
	return v;
}

/* the 4 px band only: the interior belongs to the client; never on the fullscreen window */
static void
drawborder(Win *w)
{
	if(w->id != fsid)
		border(w->img, w->img->r, Border, w->focused ? bfocus : bunfocus, ZP);
}

/* move a window image fully off-screen, then free it */
static void
retireimg(Image *i, int iswin)
{
	if(iswin)
		originwindow(i, i->r.min, addpt(screen->r.min, Pt(Bound, Bound)));
	freeimage(i);
}

/* rio's wresize order; w is placed at screen rectangle r */
static void
reshape(Win *w, Rectangle r)
{
	Image *i, *old;
	int iswin, oldwin;
	static int nreshape;

	nreshape++;
	iswin = 1;
	i = nil;
	if(failafter == 0 || nreshape < failafter)
		i = allocwindow(wscreen, r, Refbackup, DNofill);
	if(i == nil){
		fprint(2, "render: alloc win %d: %r\n", w->id);
		/* park as a plain image */
		iswin = 0;
		if(failafter == 0 || nreshape < failafter)
			i = allocimage(display, Rect(0, 0, Dx(r), Dy(r)), screen->chan, 0, DNofill);
		if(i == nil){
			fprint(2, "render: park win %d: %r\n", w->id);
			w->sr = r;	/* render_sync retries only when the size changes or on force */
			return;
		}
		dprint("render: park win %d\n", w->id);
	}
	draw(i, i->r, background, nil, ZP);
	old = w->img;
	oldwin = w->iswin;
	w->img = i;
	w->iswin = iswin;
	drawborder(w);
	term_reshape(w);

	/* pid, id and a rising count: no other live image has this name, so no retry as rio's */
	snprint(w->name, sizeof w->name, "rini.%d.%d.%d", getpid(), w->id, w->nname++);
	if(nameimage(i, w->name, 1) <= 0){	/* libdraw: 1 ok, 0 failed */
		fprint(2, "render: nameimage win %d: %r\n", w->id);
		w->name[0] = 0;
	}
	w->sr = r;
	wq_reshaped(&w->q);
	wq_moved(&w->q, 0, 0);
	if(old != nil)
		retireimg(old, oldwin);
	dprint("render: reshape win %d %dx%d %s\n", w->id, Dx(r), Dy(r), w->name);
}

/*
 * position-only move of a window image, also used to park it: the client is
 * not told to redraw; wctl readers see the new rectangle.
 */
static int
movewin(Win *w, Point pos)
{
	if(!w->iswin || eqpt(w->sr.min, pos))
		return 0;
	dprint("render: originwindow win %d -> (%d,%d)\n", w->id, pos.x, pos.y);
	originwindow(w->img, w->img->r.min, pos);
	wq_moved(&w->q, pos.x - w->img->r.min.x, pos.y - w->img->r.min.y);
	w->sr = rectaddpt(w->sr, subpt(pos, w->sr.min));
	w->q.wctlready = 1;	/* wctl reports sr: wake its readers */
	return 1;
}

/*
 * Every live window ends up placed, parked or re-topped here, and it
 * flushes. force reshapes every window, placed or hidden, so each client
 * gets a new image, name and 'r' (outer resize: the old Screen is freed
 * after this).
 */
void
render_sync(Place *p, int n, int force)
{
	static int zseq[MAXWIN], nzseq;
	static int znew[MAXWIN];	/* 1 KB: off the threadmain stack; only the main thread renders */
	Point park, pos;
	Rectangle r;
	LRect s;
	Win *w;
	int i, nz, dirty, zdirty, ww, h, focuschg;

	park = addpt(screen->r.min, Pt(Bound, Bound));
	dirty = 0;
	zdirty = 0;
	nz = 0;
	fsid = 0;
	for(w = wins; w != nil; w = w->next)
		w->seen = 0;
	for(i = 0; i < n; i++){
		w = win_lookid(p[i].id);
		s = p[i].s;
		if(p[i].layer == Lfs){
			fsid = w->id;
			s.x0 -= Border;	/* the client insets by Borderwidth */
			s.y0 -= Border;
			s.x1 += Border;
			s.y1 += Border;
		}
		ww = s.x1 - s.x0;
		h = s.y1 - s.y0;
		if(ww <= 0 || h <= 0)
			continue;
		w->seen = 1;
		if(p[i].layer == Lfs || (p[i].layer == Lfloat && p[i].vis != VisParked))
			znew[nz++] = w->id;
		pos = p[i].vis == VisParked ? park : addpt(screen->r.min, Pt(clamp(s.x0), clamp(s.y0)));
		r = rectaddpt(Rect(0, 0, ww, h), pos);
		focuschg = w->focused != p[i].focused;
		w->focused = p[i].focused;
		/* sr has the image's size, or the size whose reshape failed, or is empty */
		if(force || Dx(w->sr) != ww || Dy(w->sr) != h){
			reshape(w, r);
			dirty = 1;
			zdirty = 1;
			continue;
		}
		if(w->img == nil)	/* allocation failed, latched: nothing to move or draw */
			continue;
		if(movewin(w, pos))
			dirty = 1;
		if(focuschg){
			drawborder(w);
			dirty = 1;
		}
	}
	/* hidden: parked, and on force moved onto the new screen */
	for(w = wins; w != nil; w = w->next)
		if(!w->seen && w->img != nil){
			if(force){
				reshape(w, rectaddpt(Rect(0, 0, Dx(w->img->r), Dy(w->img->r)), park));
				dirty = 1;
			}else if(movewin(w, park))
				dirty = 1;
		}
	if(zdirty || nz != nzseq || memcmp(znew, zseq, nz * sizeof(int)) != 0){
		for(i = 0; i < nz; i++){
			w = win_lookid(znew[i]);
			if(w->img != nil && w->iswin){
				topwindow(w->img);
				dirty = 1;
			}
		}
		memmove(zseq, znew, nz * sizeof(int));
		nzseq = nz;
	}
	if(dirty || force)
		flushimage(display, 1);
}

void
render_retire(Win *w)
{
	if(w->img == nil)
		return;
	retireimg(w->img, w->iswin);
	w->img = nil;
	flushimage(display, 1);
}
