/* rule.h: window rules (rule '<regexp>' <props>). Pure table logic. */
#ifndef RULE_H
#define RULE_H

#define MAXRULE 32
#define RULESRC 256
#define WTEXT 16

enum { Pfloat = 1<<0, Ptile = 1<<1, Pwidth = 1<<2, Pws = 1<<3, Prect = 1<<4 };

typedef struct Props Props;
typedef struct Rule Rule;
typedef struct Ruletab Ruletab;

/* Pfloat and Ptile exclude each other; width is wmil unless widthpx; ws is 1-based; wtext is the width word as typed */
struct Props {
	int set;
	int width;
	int widthpx;
	char wtext[WTEXT];
	int ws;
	int rect[4];
};

struct Rule {
	char src[RULESRC];
	Rx *re;
	Props p;
};

struct Ruletab {
	Rule r[MAXRULE];
	int n;
};

/* replaces a rule with the same src; table unchanged on error */
int rule_add(Ruletab*, char *re, char **props, int nprops, char *err, int nerr);
int rule_del(Ruletab*, char *re, char *err, int nerr);
/* out zeroed, then every matching rule merged in table order, later wins per property */
void rule_match(Ruletab*, char *subject, Props *out);
/* canonical "rule 're' float|tile width workspace rect" line, no newline */
void rule_fmt(Rule*, char *buf, int n);

#endif
