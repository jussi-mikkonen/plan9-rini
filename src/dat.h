/*
 * dat.h: shared data for rini. Included after u.h, libc.h, draw.h, thread.h,
 * mouse.h, cursor.h, fcall.h, 9p.h, layout.h, desk.h, input.h, wq.h and wctl.h
 * (term.c alone also takes frame.h, tbuf.h and tkeys.h before it).
 */
typedef struct Win Win;
typedef struct Faux Faux;
typedef struct Spawned Spawned;
typedef struct Term Term;
#pragma incomplete Term	/* defined in term.c only; keeps 6l type signatures equal */

enum {
	Gap = 4,
	Border = 4,
	Bound = 100000,	/* coordinate range proven to work, relative to the viewport */
	Npend = 32
};

/* mouse button 3 menu: fixed items, then the hidden windows */
enum { MNew, MResize, MMove, MDelete, MHide, MFloat, NMenu3 };

enum { Qdir, Qcons, Qconsctl, Qcursor, Qkbd, Qkbdtap, Qlabel, Qmouse, Qscreen, Qsnarf, Qtext, Qwctl, Qwdir, Qwindow, Qwinid, Qwinname, Qwsys, Nqid };

/* the ctl tree's files, past the window files */
enum { Qctl = Nqid, Qbindings, Qrules, Qevent, Qstate, Nctlq };

#define QPATH(id, q)	(((uvlong)(id) << 8) | (q))

#define MAXSNARF	(100*1024)	/* rio's cap on a pending snarf write */

#define Edeleted	"window deleted"
#define Einuse		"file in use"
#define Etooshort	"buffer too small"
#define Eunkid		"unknown id in attach"
#define Ewindow		"cannot make window"
#define Ebadmouse	"bad format on /dev/mouse"
#define Eperm		"permission denied"
#define Enofile		"file does not exist"
#define Einterrupted	"interrupted"
#define Ebusy		"too many pending reads"

/* One client window: a tile, float or hidden entry of the desk, keyed by the layout id. */
struct Win {
	int id;
	int ref;
	int deleted;
	Image *img;
	int iswin;	/* 0 when parked as a plain image (allocwindow failed) */
	Rectangle sr;	/* last screen rectangle requested */
	int labeled;	/* a client has written /dev/label: rules re-apply once, on that first write */
	int focused;
	int seen;	/* render.c: placed in the current sync */
	char name[64];	/* current image name */
	int nname;
	char *label;
	char *wdir;
	Cursor cursor;
	int hascursor;
	int notefd;
	int consctlopen;
	int wctlopen;
	Wq q;
	Term *term;
	int winnameread;	/* a winname read since the last resize or mouse close */
	Req *pend[Npend];
	int npend;
	Win *next;
};

/* fid->aux; w is nil for a `none` or ctl attach */
struct Faux {
	Win *w;
	int file;
	int open;
	int ctl;	/* a ctl attach: file is a Q* of the ctl tree */
	int mode;	/* open mode & 3 */
	char part[UTFmax];	/* cons write: incomplete rune carried to the next write */
	int npart;
	char *sbuf;	/* snarf write: accumulated until close */
	long nsbuf;
	void *evq;	/* event: this reader's Evq, linked through evnext while open; void* because ev.h
			 * is not in every file and 6l -T compares struct contents (as Term) */
	Req *evpend;	/* event: the one parked read */
	Faux *evnext;
};

struct Spawned {
	int id;
	int pid;
};

extern int debug;
extern Screen *wscreen;
extern Image *background;
extern ulong bgcolor;
extern Desk desk;
extern Mousectl *mc;
extern Win *wins;
extern int cfd, srvfd;
extern char *user;
extern int snarfparent;
extern Channel *reqc, *clunkc, *spawnc;
extern char *spawncmd;
extern Channel *tickc;
