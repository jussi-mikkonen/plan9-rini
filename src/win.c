/* win.c: client window objects; lifetime follows references. */
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

Win *wins;

Win*
win_new(int wmil)
{
	Win *w;
	char dir[1024];
	int id;

	id = desk_add(&desk, wmil);
	if(id < 0)
		return nil;
	w = emalloc9p(sizeof *w);
	w->id = id;
	w->ref = 1;
	w->notefd = -1;
	w->label = estrdup9p("");
	w->wdir = estrdup9p(getwd(dir, sizeof dir) != nil ? dir : "/");
	w->term = term_new();
	w->next = wins;
	wins = w;
	dprint("win alloc %d\n", id);
	return w;
}

Win*
win_lookid(int id)
{
	Win *w;

	for(w = wins; w != nil; w = w->next)
		if(w->id == id)
			return w;
	return nil;
}

Win*
win_focused(void)
{
	return win_lookid(desk_focused(&desk));
}

/* take the window out of the desk (column, float or hidden) and drop everything a client could see */
static void
retire(Win *w)
{
	int i;

	w->deleted = 1;
	desk_remove(&desk, w->id);
	render_retire(w);
	for(i = 0; i < w->npend; i++)
		respond(w->pend[i], Edeleted);
	w->npend = 0;
	tap_retire(w);
}

typedef struct Note Note;
struct Note {
	int fd;
	char *msg;	/* string literal */
};

static void
noteproc(void *v)
{
	Note *n;

	n = v;
	threadsetname("note");
	write(n->fd, n->msg, strlen(n->msg));
	close(n->fd);
	free(n);
}

void
win_note(Win *w, char *msg)
{
	Note *n;

	if(w->notefd < 0)
		return;
	n = emalloc9p(sizeof *n);
	n->fd = dup(w->notefd, -1);
	n->msg = msg;
	proccreate(noteproc, n, 4096);
}

void
win_delete(Win *w)
{
	if(w->deleted)
		return;
	retire(w);
	win_note(w, "hangup");
}

void
win_incref(Win *w)
{
	w->ref++;
}

void
win_decref(Win *w)
{
	Win **pp;

	if(--w->ref > 0)
		return;
	if(!w->deleted)
		retire(w);
	if(w->notefd >= 0)
		close(w->notefd);
	term_free(w->term);
	free(w->label);
	free(w->wdir);
	for(pp = &wins; *pp != w; pp = &(*pp)->next)
		;
	*pp = w->next;
	dprint("win free %d\n", w->id);
	free(w);
}

void
win_setpid(Win *w, int pid, int dolabel)
{
	char buf[64];

	if(w->notefd >= 0)
		close(w->notefd);
	snprint(buf, sizeof buf, "/proc/%d/notepg", pid);
	w->notefd = open(buf, OWRITE|OCEXEC);
	if(dolabel){
		free(w->label);
		snprint(buf, sizeof buf, "rc %d", pid);
		w->label = estrdup9p(buf);
	}
}
