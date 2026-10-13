/* layout.h: pure niri-style scrolling column layout. No libdraw. */
#ifndef LAYOUT_H
#define LAYOUT_H
#define MAXCOL 64
#define NPRESET 3
#define DEFWIDTH 500
#define MINWIDTH 100
#define MAXWIDTH 1000
#define WSTEP 100
#define MAXSTACK 8
#define MINHPX 64
#define HWMAX 10000
#define MAXPLACE (MAXCOL*MAXSTACK)

enum { CenterNever, CenterAlways, CenterOverflow };
enum { VisOff, VisPart, VisFull, VisParked };
enum { Ltile, Lfs, Lfloat };

typedef struct LRect LRect;
typedef struct Col Col;
typedef struct Strip Strip;
typedef struct Place Place;

/* half-open: x1 and y1 are exclusive */
struct LRect {
	int x0, y0, x1, y1;
};

/* win holds window ids top to bottom, hw the height weights (0 = auto),
 * active the focused index in the stack, savew the pre-maximize width
 * (0 when not maximized) */
struct Col {
	int n;
	int win[MAXSTACK];
	int hw[MAXSTACK];
	int wmil;
	int savew;
	int active;
};

/* focus and prevfocus are indices, -1 when none */
struct Strip {
	Col c[MAXCOL];
	int n;
	int focus;
	int prevfocus;
	int viewx;
	int vw, vh;
	int gap;
	int center;
};

/* one per window; x is the strip-space left edge, px the pixel width,
 * s the screen rect, col the column index */
struct Place {
	int id;
	int layer;
	int col;
	int wmil;
	int x;
	int px;
	LRect s;
	int vis;
	int focused;
};

void strip_init(Strip*, int vw, int vh, int gap);
int strip_add(Strip*, int id, int wmil);
int strip_inscol(Strip*, Col*);	/* -1 when full */
int strip_idx(Strip*, int id);
int strip_focused(Strip*);
int strip_closeid(Strip*, int id);
void strip_focusid(Strip*, int id);
void strip_setwidthid(Strip*, int id, int wmil);
int strip_pxtomil(Strip*, int px);
void strip_focus(Strip*, int dir);
void strip_focusidx(Strip*, int idx);
void strip_preset(Strip*, int dir);
void strip_adjust(Strip*, int dmil);
void strip_setwidth(Strip*, int wmil);
void strip_resize(Strip*, int vw, int vh);
void strip_setgap(Strip*, int gap);
void strip_center(Strip*);
void strip_follow(Strip*);
int strip_consume(Strip*);
int strip_expel(Strip*);
int strip_consumeorexpel(Strip*, int dir);
int strip_focuswin(Strip*, int dir);	/* 1 at the stack edge */
void strip_movecol(Strip*, int dir);
int strip_movewin(Strip*, int dir);	/* 1 at the stack edge */
void strip_setshare(Strip*, int share);
void strip_adjshare(Strip*, int dshare);
void strip_presetheight(Strip*);
void strip_resetheight(Strip*);
void strip_setheightpx(Strip*, int id, int px);
void strip_maximize(Strip*);
int strip_place(Strip*, Place*);	/* Place must hold MAXPLACE */
#endif
