/*
 * tbuf.c: terminal window line discipline. Pure. Follows rio: ^D is
 * consumed, not delivered (a lone ^D reads 0 bytes); a rune that does not
 * fit a read leaves its tail in rpart; hold and raw mode are counted, and
 * rawoff replays the queue through tbuf_key.
 * Differences from rio: the history trims by the caller's hiwater/lowater at a
 * line boundary and never past qh (and, as rio, only for inserts at or
 * after qh); the view origin is not known here, so
 * the caller adjusts it by Tbuf.trimmed. The raw queue holds runes, not
 * keys, so a replayed rune is always inserted as typed text. Up/Down move
 * the cursor over newline-delimited lines, not wrapped screen lines; the
 * view actions (scroll, page, top) are the caller's and change nothing
 * here. Typing and the kill keys delete the selection first; the caller
 * snarfs it. Newline inserts '\n' as typed text, also above qh.
 */
#include "compat.h"
#include "input.h"
#include "tbuf.h"
#include "tkeys.h"

void
tbuf_init(Tbuf *t, long hiwater, long lowater)
{
	memset(t, 0, sizeof *t);
	t->goal = t->chg = -1;
	t->hiwater = hiwater;
	t->lowater = lowater;
}

void
tbuf_free(Tbuf *t)
{
	free(t->r);
	free(t->raw);
	t->r = t->raw = nil;
	t->nr = t->cap = t->nraw = 0;
}

static void
chg(Tbuf *t, long q)
{
	if(t->chg < 0 || q < t->chg)
		t->chg = q;
}

/* insert without trimming */
static void
ins(Tbuf *t, long q, Rune *s, long n)
{
	if(t->nr + n > t->cap){
		long c;

		c = t->cap > 0 ? t->cap : 1024;
		while(c < t->nr + n)
			c *= 2;
		t->r = realloc(t->r, c * sizeof(Rune));
		t->cap = c;
	}
	memmove(t->r + q + n, t->r + q, (t->nr - q) * sizeof(Rune));
	memmove(t->r + q, s, n * sizeof(Rune));
	t->nr += n;
	if(q <= t->q0)
		t->q0 += n;
	if(q <= t->q1)
		t->q1 += n;
	if(q < t->qh)
		t->qh += n;
	chg(t, q);
}

static void
shift(long *x, long a, long b)
{
	if(*x > b)
		*x -= b - a;
	else if(*x > a)
		*x = a;
}

void
tbuf_delete(Tbuf *t, long q0, long q1)
{
	if(q1 <= q0)
		return;
	memmove(t->r + q0, t->r + q1, (t->nr - q1) * sizeof(Rune));
	t->nr -= q1 - q0;
	shift(&t->qh, q0, q1);
	shift(&t->q0, q0, q1);
	shift(&t->q1, q0, q1);
	chg(t, q0);
}

/* drop text from the top at a line boundary once past hiwater, never past qh */
static void
trim(Tbuf *t)
{
	long m, j;

	if(t->nr <= t->hiwater)
		return;
	m = t->nr - t->lowater;
	if(m > 0 && t->r[m-1] != '\n'){
		for(j = m; j < t->qh; j++)
			if(t->r[j] == '\n'){
				m = j + 1;
				break;
			}
	}
	if(m > t->qh)
		m = t->qh;
	if(m <= 0)
		return;
	tbuf_delete(t, 0, m);
	t->trimmed += m;
}

/* only an insert at or after the output point trims, so typing into old text is never cut */
void
tbuf_insert(Tbuf *t, long q, Rune *s, long n)
{
	ins(t, q, s, n);
	if(q >= t->qh)
		trim(t);
}

void
tbuf_output(Tbuf *t, Rune *s, long n)
{
	ins(t, t->qh, s, n);
	t->qh += n;
	trim(t);
}

long
tbuf_utf8(char *part, int *npart, char *s, long n, Rune *out)
{
	char d[TUTFMAX];
	long i, tot, nout;
	int k, avail, need, r, c;

	tot = *npart + n;
	nout = 0;
	i = 0;
	while(i < tot){
		avail = tot - i < TUTFMAX ? tot - i : TUTFMAX;
		for(k = 0; k < avail; k++)
			d[k] = i + k < *npart ? part[i+k] : s[i+k - *npart];
		c = (unsigned char)d[0];
		need = c >= 0xC2 && c < 0xE0 ? 2 : c >= 0xE0 && c < 0xF0 ? 3 : c >= 0xF0 && c <= 0xF4 ? 4 : 1;
		if(avail < need){
			for(k = 1; k < avail && (d[k] & 0xC0) == 0x80; k++)
				;
			if(k == avail)
				break;
		}
		k = input_utf8dec(d, avail, &r);
		if(k < 0){
			r = 0xFFFD;
			k = 1;
		}
		if(r != 0)	/* NULs are dropped, as rio cvttorunes: libframe boxes are C strings */
			out[nout++] = r;
		i += k;
	}
	/* the carry is the tail d[0..avail), at most TUTFMAX-1 bytes */
	for(k = 0; i + k < tot; k++)
		d[k] = i + k < *npart ? part[i+k] : s[i+k - *npart];
	memmove(part, d, k);
	*npart = k;
	return nout;
}

int
tbuf_ready(Tbuf *t)
{
	long i;

	if(t->holding)
		return 0;
	if(t->nrpart > 0)
		return 1;
	if(t->rawing)
		return t->qh < t->nr || t->nraw > 0;
	for(i = t->qh; i < t->nr; i++)
		if(t->r[i] == '\n' || t->r[i] == 0x04)
			return 1;
	return 0;
}

int
tbuf_read(Tbuf *t, char *buf, int n)
{
	char e[TUTFMAX];
	Rune c;
	int i, w;

	i = t->nrpart < n ? t->nrpart : n;
	memmove(buf, t->rpart, i);
	t->nrpart -= i;
	memmove(t->rpart, t->rpart + i, t->nrpart);
	while(i < n && (t->qh < t->nr || t->nraw > 0)){
		if(t->qh < t->nr)
			c = t->r[t->qh++];
		else{
			c = t->raw[0];
			t->nraw--;
			memmove(t->raw, t->raw + 1, t->nraw * sizeof(Rune));
		}
		if(!t->rawing && c == 0x04)
			break;
		w = input_utf8enc(c, e, TUTFMAX);
		if(w < 0){
			e[0] = '?';
			w = 1;
		}
		if(i + w > n){
			memmove(buf + i, e, n - i);
			t->nrpart = i + w - n;
			memmove(t->rpart, e + n - i, t->nrpart);
			i = n;
			break;
		}
		memmove(buf + i, e, w);
		i += w;
		if(!t->rawing && c == '\n')
			break;
	}
	if(i == n && !t->rawing && t->qh < t->nr && t->r[t->qh] == 0x04)
		t->qh++;
	return i;
}

/* leaving raw mode: queued runes are typed at the cursor, as rio's wkeyctl does */
static int
rawoff(Tbuf *t)
{
	Rune *q;
	long i, n;
	int f;

	t->rawing = 0;
	q = t->raw;
	n = t->nraw;
	t->raw = nil;
	t->nraw = 0;
	f = TFwake;
	for(i = 0; i < n; i++)
		f |= tbuf_key(t, Tinsert, q[i], 0);
	free(q);
	return f;
}

int
tbuf_ctl(Tbuf *t, int c)
{
	int f;

	f = 0;
	switch(c){
	case Crawon:	/* clears hold first, as rio's xfidwrite */
		if(t->holding){
			t->holding = 0;
			f = TFhold | TFwake;
		}
		if(t->rawing++ == 0)
			f |= TFwake;
		break;
	case Crawoff:
		if(t->rawing > 0 && --t->rawing == 0)
			f = rawoff(t);
		break;
	case Choldon:
		if(t->holding++ == 0)
			f = TFhold;
		break;
	case Choldoff:
		if(t->holding > 0 && --t->holding == 0)
			f = TFhold | TFwake;
		break;
	case Creset:
		if(t->rawing > 0)
			f |= rawoff(t);
		if(t->holding > 0)
			f |= TFhold | TFwake;
		t->holding = 0;
		break;
	}
	return f;
}

int
tbuf_rawkey(Tbuf *t, Rune *s, long n)
{
	t->raw = realloc(t->raw, (t->nraw + n) * sizeof(Rune));
	memmove(t->raw + t->nraw, s, n * sizeof(Rune));
	t->nraw += n;
	return TFwake;
}

long
tbuf_bol(Tbuf *t, long q)
{
	while(q > 0 && t->r[q-1] != '\n')
		q--;
	return q;
}

long
tbuf_eol(Tbuf *t, long q)
{
	while(q < t->nr && t->r[q] != '\n')
		q++;
	return q;
}

/* rio's wbacknl */
long
tbuf_backnl(Tbuf *t, long q, long n)
{
	long i, j;

	if(n == 0 && q > 0 && t->r[q-1] != '\n')
		n = 1;
	for(i = n; i-- > 0 && q > 0;){
		--q;
		if(q == 0)
			break;
		for(j = 128; --j > 0 && q > 0; q--)
			if(t->r[q-1] == '\n')
				break;
	}
	return q;
}

void
tbuf_clean(Tbuf *t)
{
	t->chg = -1;
	t->trimmed = 0;
}

static int
isword(Rune r)
{
	return r >= 0x80 || r == '_' || (r >= '0' && r <= '9') || (r >= 'a' && r <= 'z') || (r >= 'A' && r <= 'Z');
}

/* rio's wbswidth: runes to erase back from q0 for backspace, kill-word, kill-line */
static long
bswidth(Tbuf *t, int act)
{
	long q, stop;
	int skipping, eq;
	Rune r;

	if(act == Tbackspace)
		return 1;
	q = t->q0;
	stop = q > t->qh ? t->qh : 0;
	skipping = 1;
	while(q > stop){
		r = t->r[q-1];
		if(r == '\n'){		/* eat at most one more character */
			if(q == t->q0)
				q--;
			break;
		}
		if(act == Tkillword){
			eq = isword(r);
			if(eq && skipping)
				skipping = 0;
			else if(!eq && !skipping)
				break;
		}
		q--;
	}
	return t->q0 - q;
}

static int
move(Tbuf *t, long q)
{
	t->q0 = t->q1 = q;
	return TFshow;
}

/* Esc, and Del while held, toggle hold */
static int
hold(Tbuf *t)
{
	if(t->holding)
		t->holding--;
	else
		t->holding++;
	return TFhold | (t->holding == 0 ? TFwake : 0);
}

/* C64 Return on old text: the line under the cursor replaces the pending input and is sent */
static int
resend(Tbuf *t)
{
	Rune *s;
	long a, n, p;

	a = tbuf_bol(t, t->q0);
	n = tbuf_eol(t, t->q0) - a;
	p = 0;
	if(t->nprompt > 0 && n >= t->nprompt && memcmp(t->r + a, t->prompt, t->nprompt * sizeof(Rune)) == 0)
		p = t->nprompt;
	else if(n >= 2 && t->r[a] == ';' && t->r[a+1] == ' ')
		p = 2;
	n -= p;
	s = malloc((n + 1) * sizeof(Rune));
	memmove(s, t->r + a + p, n * sizeof(Rune));
	s[n] = '\n';
	tbuf_delete(t, t->qh, t->nr);
	tbuf_insert(t, t->qh, s, n + 1);
	free(s);
	return move(t, t->nr) | TFwake;
}

int
tbuf_key(Tbuf *t, int act, Rune r, int c64)
{
	long g, q, n;
	int f;

	switch(act){
	case Tlineup:
	case Tlinedown:
	case Tpageup:
	case Tpagedown:
	case Ttop:
		return 0;	/* view actions are the caller's */
	}
	g = t->goal;
	t->goal = -1;
	switch(act){
	case Ttab:
		if(t->q0 > 0 && t->r[t->q0-1] != ' ' && t->r[t->q0-1] != '\t' && t->r[t->q0-1] != '\n')
			return TFcomplete;
		r = '\t';
		/* fall through */
	case Tnewline:
	case Tinsert:
		if(act == Tnewline)
			r = '\n';
		tbuf_delete(t, t->q0, t->q1);
		tbuf_insert(t, t->q0, &r, 1);
		return TFshow;
	case Tleft:
		return move(t, t->q0 > 0 ? t->q0 - 1 : 0);
	case Tright:
		return move(t, t->q1 < t->nr ? t->q1 + 1 : t->nr);
	case Tup:
	case Tdown:
		q = tbuf_bol(t, t->q0);
		n = g >= 0 ? g : t->q0 - q;
		t->goal = n;
		if(act == Tup){
			if(q == 0)
				return 0;
			q = tbuf_bol(t, q - 1);
		}else{
			q = tbuf_eol(t, t->q0);
			if(q == t->nr)
				return 0;
			q++;
		}
		return move(t, q + n < tbuf_eol(t, q) ? q + n : tbuf_eol(t, q));
	case Tend:
		return move(t, t->nr);
	case Toutput:
		return move(t, t->qh);
	case Tbol:
		q = tbuf_bol(t, t->q0);
		return move(t, t->q0 > t->qh && q < t->qh ? t->qh : q);
	case Teol:
		return move(t, tbuf_eol(t, t->q0));
	case Tbackspace:
	case Tkillword:
	case Tkillline:
		if(t->q0 != t->q1){
			tbuf_delete(t, t->q0, t->q1);
			return TFshow;
		}
		if(t->q0 == 0 || t->q0 == t->qh)
			return 0;
		tbuf_delete(t, t->q0 - bswidth(t, act), t->q0);
		return TFshow;
	case Thold:
		return hold(t);
	case Tintr:
		f = t->holding ? hold(t) : 0;
		t->qh = t->nr;
		return f | move(t, t->nr) | TFintr;
	case Tcomplete:
		return TFcomplete;
	case Treturn:
		if(t->q0 < t->qh && c64)
			return resend(t);
		if(t->q0 < t->qh)
			t->q0 = t->q1 = t->nr;
		else
			tbuf_delete(t, t->q0, t->q1);
		q = tbuf_bol(t, t->qh);
		t->nprompt = t->qh - q <= TPROMPT ? t->qh - q : 0;
		memmove(t->prompt, t->r + q, t->nprompt * sizeof(Rune));
		r = '\n';
		tbuf_insert(t, t->q0, &r, 1);
		return TFwake | TFshow;
	}
	return 0;
}

static Rune left1[] = { '{', '[', '(', '<', 0xAB, 0 };
static Rune right1[] = { '}', ']', ')', '>', 0xBB, 0 };
static Rune left2[] = { '\n', 0 };
static Rune left3[] = { '\'', '"', '`', 0 };
enum { Nspan = 3 };
static Rune *left[Nspan] = { left1, left2, left3 };
static Rune *right[] = { right1, left2, left3 };

static int
runeat(Rune *s, Rune c)
{
	int i;

	for(i = 0; s[i] != 0; i++)
		if(s[i] == c)
			return i;
	return -1;
}

/* rio's wclickmatch: scan from *q for the cr that closes cl */
static int
clickmatch(Tbuf *t, Rune cl, Rune cr, int dir, long *q)
{
	Rune c;
	int nest;

	nest = 1;
	for(;;){
		if(dir > 0){
			if(*q == t->nr)
				break;
			c = t->r[(*q)++];
		}else{
			if(*q == 0)
				break;
			c = t->r[--*q];
		}
		if(c == cr){
			if(--nest == 0)
				return 1;
		}else if(c == cl)
			nest++;
	}
	return cl == '\n' && nest == 1;
}

/* rio's wstretchsel for a double click: pair, bracket, line or quote span, else the word */
void
tbuf_dclick(Tbuf *t, long pt, long *q0, long *q1)
{
	Rune c, lc, rc;
	long q;
	int i, p;

	*q0 = *q1 = pt;
	lc = pt > 0 ? t->r[pt-1] : '\n';
	rc = pt < t->nr ? t->r[pt] : '\n';
	for(i = 0; i < Nspan; i++){
		p = runeat(left[i], lc);
		if(p >= 0 && right[i][p] == rc){
			if(pt > 0 && lc != '\n')
				(*q0)--;
			if(pt < t->nr)
				(*q1)++;
			return;
		}
	}
	for(i = 0; i < Nspan; i++){
		q = pt;
		c = q == 0 ? '\n' : t->r[q-1];
		p = runeat(left[i], c);
		if(p >= 0){
			if(clickmatch(t, c, right[i][p], 1, &q))
				*q1 = q - (c != '\n');
			return;
		}
		c = q == t->nr ? '\n' : t->r[q];
		p = runeat(right[i], c);
		if(p >= 0){
			if(clickmatch(t, c, left[i][p], -1, &q)){
				*q1 = pt + (pt < t->nr && c == '\n');
				*q0 = q;
				if(c != '\n' || q != 0 || t->r[0] == '\n')
					(*q0)++;
			}
			return;
		}
	}
	while(*q1 < t->nr && isword(t->r[*q1]))
		(*q1)++;
	while(*q0 > 0 && isword(t->r[*q0-1]))
		(*q0)--;
}

/* start of the word ending at q for completion (rio's windfilewidth) */
long
tbuf_wordstart(Tbuf *t, long q)
{
	Rune r;

	while(q > 0){
		r = t->r[q-1];
		if(r <= ' ' || r == '=' || r == '^' || r == '(' || r == '{')
			break;
		q--;
	}
	return q;
}

/* rio's wsend: the selection goes to the end of the input (the raw queue when raw), newline added */
int
tbuf_send(Tbuf *t)
{
	Rune *s;
	long n;

	n = t->q1 - t->q0;
	if(n <= 0)
		return 0;
	s = malloc((n + 1) * sizeof(Rune));
	memmove(s, t->r + t->q0, n * sizeof(Rune));
	if(s[n-1] != '\n' && s[n-1] != 0x04)
		s[n++] = '\n';
	if(t->rawing)
		tbuf_rawkey(t, s, n);
	else
		tbuf_insert(t, t->nr, s, n);
	free(s);
	return move(t, t->nr) | TFwake;
}
