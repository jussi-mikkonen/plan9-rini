/* ctl.h: /mnt/rini/ctl line grammar. Pure. */
#ifndef CTL_H
#define CTL_H

#define CTLLINE 1024	/* max bytes per ctl line */
#define CTLMAXARG 16

enum { Cbind, Cunbind, Ctermbind, Cset, Crule, Cunrule, Cspawn, Caction };
enum { Smod, Scenter, Sc64return, Sbackground };

typedef struct Ctlcmd Ctlcmd;

/*
 * Field use per verb:
 *  Cbind: arg[0] chord, arg[1] "once" or "repeat" (once when absent), rest = raw action text
 *  Cunbind: arg[0] chord
 *  Ctermbind: arg[0] key, arg[1] edit-action
 *  Cset: set = S*, arg[0] value; Sbackground fills colour; Sc64return accepts on|off
 *  Crule: arg[0] regexp, arg[1..] property words
 *  Cunrule: arg[0] regexp
 *  Cspawn: rest = raw text after the word spawn ("" for plain spawn)
 *  Caction: rest = the whole line
 */
struct Ctlcmd {
	int verb;
	int set;
	char *arg[CTLMAXARG];
	int narg;
	char *rest;
	unsigned long colour;
};

/* past blanks: space, tab and newline */
char *ctl_skip(char*);
/* next blank separated word, ended with a NUL in place; nil at end of line */
char *ctl_word(char **pp);
/* in place; '...' quoting with '' for a quote; token count, or -1 with a message in err */
int ctl_tokenize(char *s, char **tok, int max, char *err, int nerr);
/* control bytes become space; bare when safe, else '...'; truncates to n; returns bytes written */
int ctl_quote(char *buf, int n, char *s);
/* in place; 0 or -1 with a message in err */
int ctl_parse(char *line, Ctlcmd *c, char *err, int nerr);
/* 0xRRGGBBAA exactly; 0 or -1 with a message in err */
int ctl_parsecolour(char *s, unsigned long *rgba, char *err, int nerr);
/* the six-line ctl read text; ws is 1-based, focus 0 when none */
int ctl_summary(char *buf, int n, char *mod, char *center, int c64, unsigned long bg, int focus, int ws);

#endif
