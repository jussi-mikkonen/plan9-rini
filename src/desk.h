/* desk.h: pure workspaces: a vertical list of strips plus floats. No libdraw. */
#ifndef DESK_H
#define DESK_H

#include "layout.h"

#define MAXWS 16
#define MAXFL 32
#define MAXHID 32
#define MAXWIN 256

typedef struct Fl Fl;
typedef struct Ws Ws;
typedef struct Desk Desk;

/* r is viewport-relative */
struct Fl {
	int id;
	LRect r;
};

/* fl is bottom to top, fs the fullscreen id or 0, layer Ltile or Lfloat */
struct Ws {
	Strip st;
	Fl fl[MAXFL];
	int nfl;
	int fs;
	int layer;
};

struct Desk {
	Ws ws[MAXWS];
	int nws, cur;
	int hid[MAXHID];
	int nhid;
	int nextid;
	int nwin;
	int vw, vh, gap, center;
};

void desk_norm(Desk*);
void desk_init(Desk*, int vw, int vh, int gap);
int desk_add(Desk*, int wmil);	/* new id, -1 when full */
int desk_remove(Desk*, int id);	/* -1 if unknown */
int desk_focused(Desk*);	/* 0 when none */
void desk_focus(Desk*, int id);
Strip *desk_cur(Desk*);
Strip *desk_stripof(Desk*, int id);	/* nil unless id is a tile */
void desk_focusws(Desk*, int idx);
void desk_movecolws(Desk*, int idx);
void desk_movewinws(Desk*, int idx);
void desk_movewinws_id(Desk*, int id, int idx);	/* tile or float to ws idx, cur and focus unchanged; no-op when unusable */
int desk_wsof(Desk*, int id);	/* ws index of a tile or float, -1 when hidden or unknown */
void desk_resize(Desk*, int vw, int vh);
void desk_setcenter(Desk*, int mode);
int desk_place(Desk*, Place*);	/* Place must hold MAXWIN */

/* implemented with floats, hidden and fullscreen */
void desk_togglefloat(Desk*, int id);
void desk_floatset(Desk*, int id, LRect r);
int desk_floatrect(Desk*, int id, LRect*);
int desk_isfloat(Desk*, int id);
void desk_raise(Desk*, int id);
void desk_lower(Desk*, int id);
void desk_switchlayer(Desk*);
void desk_floatfocus(Desk*, int dir);
void desk_hide(Desk*, int id);
void desk_unhide(Desk*, int id);
int desk_ishidden(Desk*, int id);
void desk_fullscreen(Desk*);
#endif
