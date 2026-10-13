/*
 * overlay.c: the hotkey overlay and the one-line note. Both are Refbackup
 * layer windows on wscreen, raised above every client, so closing one restores
 * what was under it. Nothing here loops on the mouse: rini.c keeps the state
 * and 9P keeps being served while a box is up.
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
#include "bind.h"
#include "input.h"
#include "wq.h"
#include "wctl.h"
#include "dat.h"
#include "fns.h"

enum {
	Pad = 8,
	Gapx = 24,
	Edge = 2
};

static Image *boxbg, *boxedge;

/* lines in columns of as many rows as fit, centred, clipped to the screen; nil on failure */
static Image*
box(char **lines, int n)
{
	Image *w;
	Rectangle r;
	Point p;
	int colw[MAXBIND];
	int rows, cols, i, c, h, tw, wd;

	if(n < 1)
		return nil;
	if(boxbg == nil)
		boxbg = solid(0x222222FF);
	if(boxedge == nil)
		boxedge = solid(0xFFD700FF);
	if(boxbg == nil || boxedge == nil)
		return nil;
	h = font->height;
	rows = (Dy(screen->r) - 4*h) / h;
	if(rows < 1)
		rows = 1;
	if(rows > n)
		rows = n;
	cols = (n + rows - 1) / rows;
	for(c = 0; c < cols; c++)
		colw[c] = 0;
	for(i = 0; i < n; i++){
		tw = stringwidth(font, lines[i]);
		if(tw > colw[i/rows])
			colw[i/rows] = tw;
	}
	wd = 2*Pad + (cols - 1)*Gapx;
	for(c = 0; c < cols; c++)
		wd += colw[c];
	p = Pt((Dx(screen->r) - wd) / 2, (Dy(screen->r) - (rows*h + 2*Pad)) / 2);
	r = rectaddpt(Rect(0, 0, wd, rows*h + 2*Pad), addpt(screen->r.min, p));
	if(rectclip(&r, screen->r) == 0)
		return nil;
	w = allocwindow(wscreen, r, Refbackup, DNofill);
	if(w == nil)
		return nil;
	draw(w, r, boxbg, nil, ZP);
	border(w, r, Edge, boxedge, ZP);
	p = Pt(r.min.x + Pad, r.min.y + Pad);
	for(i = 0; i < n; i++){
		if(i > 0 && i % rows == 0)
			p = Pt(p.x + colw[i/rows - 1] + Gapx, r.min.y + Pad);
		string(w, p, display->white, ZP, font, lines[i]);
		p.y += h;
	}
	topwindow(w);
	flushimage(display, 1);
	return w;
}

/* the same data as the bindings file: bind_fmt plus the action */
Image*
overlay_bindings(struct Bindtab *bt)
{
	static char buf[MAXBIND][ACTLEN+32];
	static char *lines[MAXBIND];
	char chord[ACTLEN];
	int i;

	for(i = 0; i < bt->n; i++){
		bind_fmt(&bt->b[i], chord, sizeof chord);
		snprint(buf[i], sizeof buf[i], "%s  %s", chord, bt->b[i].action);
		lines[i] = buf[i];
	}
	return box(lines, bt->n);
}

Image*
overlay_note(char *text)
{
	return box(&text, 1);
}

void
overlay_close(Image *i)
{
	freeimage(i);
	flushimage(display, 1);
}
