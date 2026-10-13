/*
 * spawn.c: launch a client for a window. One short-lived proc per launch
 * mounts rini for the window in its own namespace and forks the client in
 * its own note group; the pid goes back to main over spawnc (never waited
 * for in main).
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

Channel *spawnc;
char *spawncmd;

typedef struct Sarg Sarg;
struct Sarg {
	int id;
	char *cmd;
	char *dir;
};

/*
 * The caller's own namespace for a client: /dev from the attach spec
 * ("<id>" for a window, "none" for the startup script) and /mnt/rini. -1
 * with errstr when the window files cannot be mounted.
 */
static int
clientns(char *spec)
{
	int fd;

	rfork(RFNAMEG|RFENVG|RFFDG);
	/* 9front's mount closes the fd it is given, so the first mount gets a copy */
	fd = dup(cfd, -1);
	if(fd < 0 || mount(fd, -1, "/mnt/wsys", MREPL, spec) < 0 || bind("/mnt/wsys", "/dev", MBEFORE) < 0)
		return -1;
	if(mount(cfd, -1, "/mnt/rini", MREPL, "ctl") < 0)
		fprint(2, "rini: mount /mnt/rini: %r\n");
	return 0;
}

/*
 * id 0 is the startup script, rio's initcmd: rc cmd in a none attach with
 * rini's stdio. Else rc -c cmd (rc -i when nil) on the window's cons.
 */
static void
spawnproc(void *v)
{
	Sarg *a;
	Spawned s;
	char spec[16];
	int pid;

	threadsetname("spawn");
	a = v;
	s.id = a->id;
	s.pid = 0;
	snprint(spec, sizeof spec, "%d", a->id);
	if(clientns(a->id ? spec : "none") < 0){
		fprint(2, "spawn %d: mount: %r\n", a->id);
		goto done;
	}
	pid = rfork(RFPROC|RFNOTEG|RFFDG);
	if(pid == 0){
		if(a->id){
			close(0);
			open("/dev/cons", OREAD);
			close(1);
			open("/dev/cons", OWRITE);
			dup(1, 2);
		}
		close(cfd);
		close(srvfd);	/* else the mount never sees EOF when rini dies */
		if(a->dir != nil)
			chdir(a->dir);
		if(a->id == 0)
			execl("/bin/rc", "rc", a->cmd, nil);
		else if(a->cmd != nil)
			execl("/bin/rc", "rc", "-c", a->cmd, nil);
		else
			execl("/bin/rc", "rc", "-i", nil);
		fprint(2, "spawn: exec rc: %r\n");
		_exits("exec");
	}
	if(pid > 0)
		s.pid = pid;
done:
	send(spawnc, &s);
	free(a->cmd);
	free(a->dir);
	free(a);
	threadexits(nil);
}

static void
launch(int id, char *cmd, char *dir)
{
	Sarg *a;

	a = emalloc9p(sizeof *a);
	a->id = id;
	a->cmd = cmd != nil ? estrdup9p(cmd) : nil;
	a->dir = dir != nil ? estrdup9p(dir) : nil;
	proccreate(spawnproc, a, 16384);
}

void
spawn(int id, char *cmd, char *dir)
{
	Win *w;

	w = win_lookid(id);
	if(dir != nil){
		free(w->wdir);
		w->wdir = estrdup9p(dir);
	}
	rules_apply(w, cmd != nil ? cmd : "rc");	/* the command is the open-time subject */
	launch(id, cmd, dir);
}

void
startup(char *file)
{
	launch(0, file, nil);
}
