/*
 * fs.c: lib9p plumbing. The srv proc only forwards (fwd, fsdestroyfid); the
 * main thread owns all state and answers every request.
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

int cfd, srvfd;
char *user;
Channel *reqc, *clunkc;
static int mainpid;

/* the window files, then the ctl tree's */
static struct {
	char *name;
	ulong perm;
} dirtab[Nctlq] = {
	{".",		0500|DMDIR},
	{"cons",	0600},
	{"consctl",	0200},
	{"cursor",	0600},
	{"kbd",		0600},
	{"kbdtap",	0600},
	{"label",	0600},
	{"mouse",	0600},
	{"screen",	0400},
	{"snarf",	0600},
	{"text",	0600},
	{"wctl",	0600},
	{"wdir",	0600},
	{"window",	0400},
	{"winid",	0400},
	{"winname",	0400},
	{"wsys",	0500|DMDIR},
	{"ctl",		0600},
	{"bindings",	0400},
	{"rules",	0400},
	{"event",	0400},
	{"state",	0400},
};

static void
fwd(Req *r)
{
	sendp(reqc, r);
}

/*
 * Main drops fids inside respond() (failed walk, answering a read on a
 * clunked fid); it is clunkc's only reader, so it must not block on it.
 */
static void
fsdestroyfid(Fid *f)
{
	if(f->aux == nil)
		return;
	if(getpid() == mainpid)
		fs_clunk(f->aux);
	else
		sendp(clunkc, f->aux);
}

static Srv fs;

/*
 * Not threadpostsrv: its srv proc gets a copy of the fd table, so the main
 * thread's respond() would write to an fd that is not the 9P pipe. This proc
 * shares the table with main.
 */
static void
srvproc(void*)
{
	threadsetname("srv");
	srv(&fs);
}

int
fs_init(void)
{
	char path[80];
	int p[2], fd;

	user = getuser();
	snarf_init();
	mainpid = getpid();
	reqc = chancreate(sizeof(Req*), 32);
	clunkc = chancreate(sizeof(Faux*), 32);
	fs.attach = fwd;
	fs.walk = fwd;
	fs.open = fwd;
	fs.read = fwd;
	fs.write = fwd;
	fs.stat = fwd;
	fs.flush = fwd;
	fs.destroyfid = fsdestroyfid;
	snprint(path, sizeof path, "/srv/rini.%s.%d", user, getpid());
	if(pipe(p) < 0)
		return -1;
	fs.infd = fs.outfd = srvfd = p[1];
	cfd = p[0];
	/* kept open: the entry goes away when rini exits; clients do not inherit it */
	fd = create(path, OWRITE|ORCLOSE|OCEXEC, 0600);
	if(fd < 0 || fprint(fd, "%d", cfd) < 0)
		return -1;
	proccreate(srvproc, nil, 32*1024);
	putenv("wsys", path);
	return 0;
}

static Qid
qidof(Faux *a, int q)
{
	return (Qid){QPATH(a->w != nil ? a->w->id : 0, q), 0, q == Qdir || q == Qwsys ? QTDIR : QTFILE};
}

ulong
fs_perm(int q)
{
	return dirtab[q].perm;
}

static void
dirfill(Dir *d, char *name, Qid q, ulong perm)
{
	d->name = estrdup9p(name);
	d->qid = q;
	d->mode = perm;
	d->uid = estrdup9p(user);
	d->gid = estrdup9p(user);
	d->muid = estrdup9p(user);
	d->atime = d->mtime = time(0);
}

static void
filldir(Dir *d, Faux *a, int q)
{
	dirfill(d, dirtab[q].name, qidof(a, q), dirtab[q].perm);
}

/* not served: snarf when the parent has one; a none attach's window files; the other tree's */
static int
skipq(Faux *a, int q)
{
	if(q >= Nqid)
		return !a->ctl;
	if(a->ctl)
		return 1;
	if(q == Qsnarf && snarfparent)
		return 1;
	return a->w == nil && q != Qkbdtap && q != Qscreen && q != Qwctl && q != Qwsys;
}

static int
dirgen(int i, Dir *d, void *v)
{
	int q;

	for(q = 1; q < Nctlq; q++)
		if(!skipq(v, q) && i-- == 0){
			filldir(d, v, q);
			return 0;
		}
	return -1;
}

/* the live windows, as wsys/<id> */
static int
wsysgen(int i, Dir *d, void*)
{
	Win *w;
	char buf[16];

	for(w = wins; w != nil; w = w->next)
		if(!w->deleted && i-- == 0){
			snprint(buf, sizeof buf, "%d", w->id);
			dirfill(d, buf, (Qid){QPATH(w->id, Qdir), 0, QTDIR}, 0500|DMDIR);
			return 0;
		}
	return -1;
}

static void
attach(Req *r)
{
	Wctl wc;
	Faux *a;
	Win *w;
	char err[ERRMAX];
	int id, isctl;

	if(strcmp(r->ifcall.uname, user) != 0){
		respond(r, Eperm);
		return;
	}
	isctl = 0;
	switch(wctl_attach(r->ifcall.aname, &wc, &id, err, sizeof err)){
	case Anone:
		w = nil;
		break;
	case Actl:
		w = nil;
		isctl = 1;
		break;
	case Aid:
		w = win_lookid(id);
		if(w == nil || w->deleted){
			respond(r, Eunkid);
			return;
		}
		win_incref(w);
		break;
	case Anew:
		w = dev_newwin(&wc);
		if(w == nil){
			respond(r, Ewindow);
			return;
		}
		if(wc.set & Ppid)
			win_setpid(w, wc.pid, 0);
		break;
	default:
		respond(r, err);
		return;
	}
	a = emalloc9p(sizeof *a);
	a->w = w;
	a->ctl = isctl;
	a->file = Qdir;
	r->fid->aux = a;
	r->fid->qid = qidof(a, Qdir);
	r->ofcall.qid = r->fid->qid;
	respond(r, nil);
}

static char*
walk1(Fid *f, char *name, void*)
{
	Faux *a;
	Win *w;
	int i;

	a = f->aux;
	if(a->file == Qwsys){
		if(strcmp(name, "..") == 0){
			a->file = Qdir;
			f->qid = qidof(a, Qdir);
			return nil;
		}
		w = win_lookid(atoi(name));
		if(w == nil || w->deleted)
			return Enofile;
		win_incref(w);	/* Faux.w stays a counted reference */
		if(a->w != nil)
			win_decref(a->w);
		a->w = w;
		a->file = Qdir;
		f->qid = qidof(a, Qdir);
		return nil;
	}
	if(a->file != Qdir)
		return Enofile;
	if(strcmp(name, "..") == 0)
		return nil;
	for(i = 1; i < Nctlq; i++)
		if(strcmp(name, dirtab[i].name) == 0 && !skipq(a, i)){
			a->file = i;
			f->qid = qidof(a, i);
			return nil;
		}
	return Enofile;
}

static char*
clone(Fid *old, Fid *new, void*)
{
	Faux *a;

	a = emalloc9p(sizeof *a);
	*a = *(Faux*)old->aux;	/* lib9p clones only unopened fids: the open-time fields are zero */
	if(a->w != nil)
		win_incref(a->w);
	new->aux = a;
	return nil;
}

void
fs_req(Req *r)
{
	Faux *a;

	switch(r->ifcall.type){
	case Tattach:
		attach(r);
		break;
	case Twalk:
		walkandclone(r, walk1, clone, nil);
		break;
	case Topen:
		dev_open(r);
		break;
	case Tread:
		a = r->fid->aux;
		if(a->file == Qdir || a->file == Qwsys){
			dirread9p(r, a->file != Qdir ? wsysgen : dirgen, a);
			respond(r, nil);
		}else
			dev_read(r);
		break;
	case Twrite:
		dev_write(r);
		break;
	case Tstat:
		a = r->fid->aux;
		filldir(&r->d, a, a->file);
		respond(r, nil);
		break;
	case Tflush:
		dev_flush(r);
		break;
	}
}

void
fs_clunk(Faux *a)
{
	if(a->open)
		dev_close(a);
	if(a->evq != nil)
		ctlfs_evclose(a);
	if(a->w != nil)
		win_decref(a->w);
	free(a);
}
