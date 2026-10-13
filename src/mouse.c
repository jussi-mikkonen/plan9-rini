/*
 * mouse.c: the button 3 menu and its blocking rubber-band loops, in the shape
 * of rio's button3menu, sweep, drag, bandsize and pointto. 9P is not served
 * while they run (accepted, as in rio; the Mod+B1 float move is event-driven
 * in rini.c instead). Only transient band windows are allocated here
 * (allocwindow(wscreen, r, Refnone, DNofill)) and every exit path frees them.
 * Final geometry goes through the desk and relayout(); render.c stays the
 * owner of client images.
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

enum {
	B3 = 4,
	Minsweep = 5,
	Nlabel = 60	/* rio's shortlabel width in runes */
};

static Cursor crosscursor = {
	{-7, -7},
	{0x03, 0xC0, 0x03, 0xC0, 0x03, 0xC0, 0x03, 0xC0,
	 0x03, 0xC0, 0x03, 0xC0, 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF, 0x03, 0xC0, 0x03, 0xC0,
	 0x03, 0xC0, 0x03, 0xC0, 0x03, 0xC0, 0x03, 0xC0, },
	{0x00, 0x00, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80,
	 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x7F, 0xFE,
	 0x7F, 0xFE, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80,
	 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x00, 0x00, }
};

static Cursor boxcursor = {
	{-7, -7},
	{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xF8, 0x1F, 0xF8, 0x1F, 0xF8, 0x1F,
	 0xF8, 0x1F, 0xF8, 0x1F, 0xF8, 0x1F, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, },
	{0x00, 0x00, 0x7F, 0xFE, 0x7F, 0xFE, 0x7F, 0xFE,
	 0x70, 0x0E, 0x70, 0x0E, 0x70, 0x0E, 0x70, 0x0E,
	 0x70, 0x0E, 0x70, 0x0E, 0x70, 0x0E, 0x70, 0x0E,
	 0x7F, 0xFE, 0x7F, 0xFE, 0x7F, 0xFE, 0x00, 0x00, }
};

static Cursor sightcursor = {
	{-7, -7},
	{0x1F, 0xF8, 0x3F, 0xFC, 0x7F, 0xFE, 0xFB, 0xDF,
	 0xF3, 0xCF, 0xE3, 0xC7, 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF, 0xE3, 0xC7, 0xF3, 0xCF,
	 0x7B, 0xDF, 0x7F, 0xFE, 0x3F, 0xFC, 0x1F, 0xF8, },
	{0x00, 0x00, 0x0F, 0xF0, 0x31, 0x8C, 0x21, 0x84,
	 0x41, 0x82, 0x41, 0x82, 0x41, 0x82, 0x7F, 0xFE,
	 0x7F, 0xFE, 0x41, 0x82, 0x41, 0x82, 0x41, 0x82,
	 0x21, 0x84, 0x31, 0x8C, 0x0F, 0xF0, 0x00, 0x00, }
};

static char *fixed[NMenu3] = { "New", "Resize", "Move", "Delete", "Hide", "Float/Tile" };

static Image *edge[4];
static Image *bandcol;

/* rio's shortlabel: labels are client text, so always truncated into a rini-owned buffer */
static char*
shortlabel(char *s, char *buf)
{
	int i, k, l;
	Rune r;

	l = utflen(s);
	if(l < Nlabel-2)
		return strcpy(buf, s);
	k = i = 0;
	while(i < Nlabel/2){
		k += chartorune(&r, s+k);
		i++;
	}
	strncpy(buf, s, k);
	strcpy(buf+k, "...");
	while((l-i) >= Nlabel/2-4){
		k += chartorune(&r, s+k);
		i++;
	}
	strcat(buf, s+k);
	return buf;
}

static char*
menugen(int i)
{
	static char buf[MAXHID][Nlabel*UTFmax];

	if(i < NMenu3)
		return fixed[i];
	i -= NMenu3;
	if(i >= desk.nhid)
		return nil;
	return shortlabel(win_lookid(desk.hid[i])->label, buf[i]);
}

static Menu menu3 = { nil, menugen, 0 };

int
mouse_menu3(void)
{
	return menuhit(3, mc, &menu3, wscreen);
}

static Point
onscreen(Point p)
{
	if(p.x < screen->r.min.x)
		p.x = screen->r.min.x;
	if(p.x > screen->r.max.x-1)
		p.x = screen->r.max.x-1;
	if(p.y < screen->r.min.y)
		p.y = screen->r.min.y;
	if(p.y > screen->r.max.y-1)
		p.y = screen->r.max.y-1;
	return p;
}

static void
drawedge(Image **bp, Rectangle r)
{
	Image *b;

	b = *bp;
	if(b != nil && Dx(b->r) == Dx(r) && Dy(b->r) == Dy(r))
		originwindow(b, r.min, r.min);
	else{
		freeimage(b);
		b = allocwindow(wscreen, r, Refnone, DNofill);
		if(b != nil)
			draw(b, r, bandcol, nil, ZP);
		*bp = b;
	}
}

static void
bandfree(void)
{
	int i;

	for(i = 0; i < 4; i++){
		freeimage(edge[i]);
		edge[i] = nil;
	}
}

/* rio's drawborder: four edge windows on wscreen, reused while the size is unchanged */
static void
band(Rectangle r)
{
	if(bandcol == nil && (bandcol = solid(DRed)) == nil)
		return;
	r = canonrect(r);
	drawedge(&edge[0], Rect(r.min.x, r.min.y, r.min.x+Border, r.max.y));
	drawedge(&edge[1], Rect(r.min.x+Border, r.min.y, r.max.x-Border, r.min.y+Border));
	drawedge(&edge[2], Rect(r.max.x-Border, r.min.y, r.max.x, r.max.y));
	drawedge(&edge[3], Rect(r.min.x+Border, r.max.y-Border, r.max.x-Border, r.max.y));
	flushimage(display, 1);
}

/* leave the loop: no bands, default cursor, and every button up before anyone else sees the mouse */
static int
cancel(void)
{
	bandfree();
	setcursor(mc, nil);
	flushimage(display, 1);
	while(mc->buttons)
		readmouse(mc);
	return -1;
}

int
mouse_sweep(Rectangle *r)
{
	Rectangle b;
	Point p0, p;

	setcursor(mc, &crosscursor);
	while(mc->buttons == 0)
		readmouse(mc);
	p0 = onscreen(mc->xy);
	p = p0;
	b = Rpt(p0, p0);
	while(mc->buttons == B3){
		if(!eqpt(mc->xy, p)){
			p = onscreen(mc->xy);
			b = canonrect(Rpt(p0, p));
			if(Dx(b) > Minsweep && Dy(b) > Minsweep)
				band(b);
		}
		readmouse(mc);
	}
	if(mc->buttons != 0 || Dx(b) < WMINW || Dy(b) < WMINH)
		return cancel();
	bandfree();
	setcursor(mc, nil);
	*r = b;
	return 0;
}

/*
 * Waits for a B3 press, then follows the pointer while it stays down: a move
 * of the whole rectangle, or of one corner (bit 0 right, bit 1 bottom) with
 * the WMINW x WMINH minimum. The corner moves as the pointer moves from
 * press, so it does not jump to the pointer. A chord or a release without
 * motion cancels.
 */
int
mouse_track(Rectangle start, int corner, int move, Point press, Rectangle *r)
{
	Rectangle b, ob;
	Point p, p0, dm, k;

	setcursor(mc, move ? &boxcursor : &crosscursor);
	while(mc->buttons == 0)
		readmouse(mc);
	if(mc->buttons != B3)
		return cancel();
	p0 = onscreen(mc->xy);
	dm = subpt(p0, start.min);
	k = Pt(corner&1 ? start.max.x-1 : start.min.x, corner&2 ? start.max.y-1 : start.min.y);
	b = ob = start;
	band(b);
	while(mc->buttons == B3){
		p = onscreen(move ? mc->xy : addpt(k, subpt(mc->xy, press)));
		if(move)
			b = rectsubpt(Rpt(p, addpt(p, subpt(start.max, start.min))), dm);
		else{
			b = start;
			if(corner & 1)
				b.max.x = p.x+1 < start.min.x+WMINW ? start.min.x+WMINW : p.x+1;
			else
				b.min.x = p.x > start.max.x-WMINW ? start.max.x-WMINW : p.x;
			if(corner & 2)
				b.max.y = p.y+1 < start.min.y+WMINH ? start.min.y+WMINH : p.y+1;
			else
				b.min.y = p.y > start.max.y-WMINH ? start.max.y-WMINH : p.y;
		}
		if(!eqrect(b, ob)){
			band(b);
			ob = b;
		}
		readmouse(mc);
	}
	if(mc->buttons != 0 || eqrect(b, start))
		return cancel();
	bandfree();
	setcursor(mc, nil);
	*r = b;
	return 0;
}

int
mouse_pointto(Point *p)
{
	int ok;

	setcursor(mc, &sightcursor);
	while(mc->buttons == 0)
		readmouse(mc);
	ok = mc->buttons == B3;
	while(mc->buttons){
		if(mc->buttons != B3)
			ok = 0;
		readmouse(mc);
	}
	setcursor(mc, nil);
	*p = mc->xy;
	return ok ? 0 : -1;
}
