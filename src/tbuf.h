/* tbuf.h: terminal window line discipline (rune buffer, output point, cons reads). Pure; no I/O. */
#ifndef TBUF_H
#define TBUF_H

#define TPROMPT 64
#define TUTFMAX 4

enum { Crawon, Crawoff, Choldon, Choldoff, Creset };

/* flags returned by tbuf_ctl, tbuf_rawkey, tbuf_key */
enum { TFwake = 1, TFintr = 2, TFhold = 4, TFcomplete = 8, TFshow = 16 };

typedef struct Tbuf Tbuf;
struct Tbuf {
	Rune *r;
	long nr, cap;
	long qh, q0, q1;	/* output point; selection (cursor when equal) */
	int rawing, holding;
	Rune *raw;	/* raw-mode queue, grown as rio's waddraw */
	long nraw;
	char rpart[TUTFMAX];	/* encoded bytes a short read left behind */
	int nrpart;
	Rune prompt[TPROMPT];
	int nprompt;
	long goal;	/* column kept by up/down, -1 none */
	long chg;	/* lowest index changed since tbuf_clean, -1 when clean */
	long trimmed;	/* runes removed from the top since tbuf_clean */
	long hiwater, lowater;
};

void tbuf_init(Tbuf*, long hiwater, long lowater);
void tbuf_free(Tbuf*);
void tbuf_insert(Tbuf*, long q, Rune *s, long n);
void tbuf_delete(Tbuf*, long q0, long q1);
void tbuf_output(Tbuf*, Rune *s, long n);
/* decode s with a carried incomplete tail in part; out holds n+TUTFMAX runes */
long tbuf_utf8(char *part, int *npart, char *s, long n, Rune *out);
int tbuf_ready(Tbuf*);
int tbuf_read(Tbuf*, char *buf, int n);
int tbuf_ctl(Tbuf*, int c);
int tbuf_rawkey(Tbuf*, Rune *s, long n);
long tbuf_bol(Tbuf*, long q);
long tbuf_eol(Tbuf*, long q);
long tbuf_backnl(Tbuf*, long q, long n);
void tbuf_clean(Tbuf*);

/* edit keys */
int tbuf_key(Tbuf*, int act, Rune r, int c64);
void tbuf_dclick(Tbuf*, long q, long *q0, long *q1);
long tbuf_wordstart(Tbuf*, long q);
int tbuf_send(Tbuf*);

#endif
