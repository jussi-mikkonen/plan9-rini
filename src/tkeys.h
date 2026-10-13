/* tkeys.h: terminal key -> edit-action table. Pure; no I/O. */
#ifndef TKEYS_H
#define TKEYS_H

enum {	/* Tlineup..Ttop are the view keys: term.c tests that range */
	Tinsert, Tleft, Tright, Tup, Tdown, Tlineup, Tlinedown, Tpageup, Tpagedown,
	Ttop, Tend, Tkillline, Tkillword, Tbackspace, Tbol, Teol, Toutput, Thold,
	Treturn, Tintr, Tcomplete, Ttab, Tnewline, Nterm
};

#define MAXTKEY 64

typedef struct Tkey Tkey;
typedef struct Tkeytab Tkeytab;

struct Tkey {
	int shift;
	int rune;	/* as delivered in a kbd c message; Ctrl+u is 0x15 */
	int act;
};

struct Tkeytab {
	Tkey k[MAXTKEY];
	int n;
	int c64;	/* C64 Return: output and input share the screen */
};

void tkeys_init(Tkeytab*);
/* key is chord text, [Ctrl+][Shift+]key; 0 or -1 with err */
int tkeys_add(Tkeytab*, char *key, char *action, char *err, int nerr);
/* exact (shift, rune) entry, else with shift the unshifted one, else Tinsert */
int tkeys_lookup(Tkeytab*, int shift, int rune);
/* enum value of an action name, -1 if unknown */
int tkeys_action(char *name);
char *tkeys_actname(int act);
void tkeys_defaults(Tkeytab*);

#endif
