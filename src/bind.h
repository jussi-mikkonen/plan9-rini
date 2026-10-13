/* bind.h: binding table, chord and modifier parsing. */
#ifndef BIND_H
#define BIND_H

#include "action.h"

enum { MShift = 1, MCtl = 2, MAlt = 4, MMod4 = 8, MMod = 16 };
enum { RepOnce, RepRepeat };
enum { ModeMask, ModeLeader };

#define MAXBIND 128
/*
 * Mod is Mod4 by default; Ctrl+Alt is a dev-only fallback. Under Ctrl,
 * punctuation runes are layout-dependent, so Mod+n/Mod+w alias
 * Mod+minus/Mod+plus.
 */
#define FALLBACKMOD "Ctrl+Alt"

typedef struct Binding Binding;
typedef struct Bindtab Bindtab;

struct Binding {
	int mods;	/* as given; MMod means the configured WM modifier */
	int key;
	char action[ACTLEN];
	int repeat;
};

struct Bindtab {
	Binding b[MAXBIND];
	int n;
	int mode;
	int modmask;
	int leader;
	int gen;	/* bumped by bind_setmod */
};

void bind_init(Bindtab*);
/*
 * control rune to its ASCII key for matching (Ctrl held): U+0001..U+001A to
 * a..z, U+0000 to '@', U+001B..U+001F to [ \ ] ^ _; r otherwise
 */
int bind_unctl(int r);
int bind_setmod(Bindtab*, char *spec, char *err, int nerr);
/* parse Mod+Shift+r style text into modifier bits and a key; 0 or -1 with err */
int bind_parsechord(char *chord, int *mods, int *key, char *err, int nerr);
int bind_add(Bindtab*, char *chord, char *repeat, char *action, char *err, int nerr);
Binding *bind_lookup(Bindtab*, int mods, int key);
void bind_defaults(Bindtab*);
int bind_modbit(int rune);
/* a modifier key or AltGr: never a chord key */
int bind_isspecial(int rune);
/* chord text of a binding, e.g. Mod+Shift+r */
void bind_fmt(Binding*, char *buf, int n);
/*
 * "bind <chord> once|repeat <action>", the ctl line, so the bindings file
 * writes back; each ctl write must hold whole lines and stay under 8 KB
 */
void bind_line(Binding*, char *buf, int n);
/* modifier text that bind_setmod accepts back, e.g. Mod4, Ctrl+Alt, leader F12 */
void bind_modfmt(Bindtab*, char *buf, int n);
/* remove a chord; -1 with err on a bad or unbound chord */
int bind_del(Bindtab*, char *chord, char *err, int nerr);

#endif
