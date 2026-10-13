/* fns.h: rini function prototypes. */
#pragma varargck argpos dprint 1

/* rini.c */
void dprint(char*, ...);
void relayout(void);
int ui_action(char *act, int fromkbd, char *err, int nerr);	/* quit, spawn, delete, else action_run; 0 or -1 with err */
void kbd_deliver(Win*, char *msg, int len);	/* a kbd message to the window's kbd queue, else term_key for a `c` message */
int rescreen(unsigned long rgba);	/* new background colour; 0 or -1 with errstr */

/* win.c */
Win *win_new(int wmil);
Win *win_lookid(int id);
Win *win_focused(void);
void win_incref(Win*);
void win_decref(Win*);
void win_delete(Win*);
void win_note(Win*, char *msg);
void win_setpid(Win*, int pid, int dolabel);

/* term.c */
int term_init(void);
Term *term_new(void);
void term_free(Term*);
int term_visible(Win*);
void term_reshape(Win*);
void term_redraw(Win*);
void term_key(Win*, Rune r, int shift);
void term_output(Win*, Rune *s, long n);
int term_canwrite(Win*);
int term_ready(Win*);
int term_read(Win*, char *buf, int n);
char *term_text(Win*, long *nb);	/* UTF-8, malloc'd; caller frees */
void term_ctl(Win*, int c);
void term_clear(Win*);	/* OTRUNC open of text */
void term_mouse(Win*, Mouse*);
void term_tick(ulong gen);
void term_setscroll(Win*, int on);

/* render.c */
extern int failafter;	/* rini -F n, test only */
int render_init(void);	/* 0 ok, -1 on failure (caller decides) */
void render_sync(Place*, int n, int force);	/* force: every window reshaped (new Screen) */
Image *solid(ulong rgba);	/* a 1x1 replicated colour */
void render_retire(Win*);

/* dev.c */
void dev_open(Req*);
void dev_read(Req*);
void dev_write(Req*);
void dev_close(Faux*);
void dev_flush(Req*);
void dev_wakeup(Win*);
int dev_tap(int focusid, char *msg, int len);	/* -1 no tap reader: the caller delivers */
void tap_retire(Win*);	/* a tap reader or writer in w is gone; its parked read gets Edeleted */
Win *dev_newwin(struct Wctl*);	/* wctl new / attach new: the width rule, -hide */
char *dev_spawn(struct Wctl*);	/* dev_newwin plus its client; nil or Ewindow */
LRect lrect(Rectangle);
Rectangle rectof(LRect);
void snarf_init(void);
char *snarf_get(long *n);	/* emalloc9p'd; caller frees */
void snarf_put(char *s, long n);

/* fs.c */
int fs_init(void);
void fs_req(Req*);
void fs_clunk(Faux*);
ulong fs_perm(int q);	/* the file's permission bits */

/* mouse.c */
int mouse_menu3(void);	/* menu item index, -1 none; items 0..NMenu3-1 fixed, then hidden windows in desk.hid order */
int mouse_sweep(Rectangle *r);	/* 0 ok, -1 cancelled */
int mouse_track(Rectangle start, int corner, int move, Point press, Rectangle *r);	/* a resize from press, or a move; 0 ok, -1 cancelled */
int mouse_pointto(Point *p);	/* 0 ok, -1 cancelled */

/* ctlfs.c */
void ctlfs_read(Req*);	/* each answers exactly once */
void ctlfs_write(Req*);
void ctlfs_commit(void);	/* relayout: diff the desk for the event readers; free when there are none */
void ctlfs_evopen(Faux*);	/* a new event reader, queue primed with the snapshot */
void ctlfs_evclose(Faux*);
void rules_apply(Win*, char *subject);	/* float, rect, width, workspace from the matching rules */

/* spawn.c */
void spawn(int id, char *cmd, char *dir);
void startup(char *file);	/* rc file in its own note group; the pid comes back over spawnc as Spawned{0, pid} */

/* overlay.c: Refbackup layer boxes above every window; nil when nothing could be drawn */
Image *overlay_bindings(struct Bindtab*);	/* `chord  action` for every live binding */
Image *overlay_note(char *text);
void overlay_close(Image*);
