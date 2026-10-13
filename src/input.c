/* input.c: kbd message router and UTF-8 helpers. */
#include "compat.h"
#include "input.h"

int
input_utf8dec(char *s, int n, int *rune)
{
	unsigned char *u;
	int c, r, need, i, min;

	if(n < 1)
		return -1;
	u = (unsigned char*)s;
	c = u[0];
	if(c < 0x80){
		*rune = c;
		return 1;
	}
	if(c >= 0xC2 && c <= 0xDF){
		need = 1;
		r = c & 0x1F;
		min = 0x80;
	}else if(c >= 0xE0 && c <= 0xEF){
		need = 2;
		r = c & 0x0F;
		min = 0x800;
	}else if(c >= 0xF0 && c <= 0xF4){
		need = 3;
		r = c & 0x07;
		min = 0x10000;
	}else
		return -1;
	if(n < need + 1)
		return -1;
	for(i = 1; i <= need; i++){
		if((u[i] & 0xC0) != 0x80)
			return -1;
		r = (r << 6) | (u[i] & 0x3F);
	}
	if(r < min || r > 0x10FFFF || (r >= 0xD800 && r <= 0xDFFF))
		return -1;
	*rune = r;
	return need + 1;
}

int
input_utf8enc(int rune, char *buf, int n)
{
	if(rune < 0 || rune > 0x10FFFF || (rune >= 0xD800 && rune <= 0xDFFF))
		return -1;
	if(rune < 0x80){
		if(n < 1)
			return -1;
		buf[0] = rune;
		return 1;
	}
	if(rune < 0x800){
		if(n < 2)
			return -1;
		buf[0] = 0xC0 | (rune >> 6);
		buf[1] = 0x80 | (rune & 0x3F);
		return 2;
	}
	if(rune < 0x10000){
		if(n < 3)
			return -1;
		buf[0] = 0xE0 | (rune >> 12);
		buf[1] = 0x80 | ((rune >> 6) & 0x3F);
		buf[2] = 0x80 | (rune & 0x3F);
		return 3;
	}
	if(n < 4)
		return -1;
	buf[0] = 0xF0 | (rune >> 18);
	buf[1] = 0x80 | ((rune >> 12) & 0x3F);
	buf[2] = 0x80 | ((rune >> 6) & 0x3F);
	buf[3] = 0x80 | (rune & 0x3F);
	return 4;
}

enum { Leaderms = 1500 };	/* a leader tap disarms after this, there is no indicator */

void
input_init(Input *in, Bindtab *bt)
{
	memset(in, 0, sizeof *in);
	in->bt = bt;
}

static int
inlist(int *l, int n, int r)
{
	int i;

	for(i = 0; i < n; i++)
		if(l[i] == r)
			return 1;
	return 0;
}

/* leader state from before a set mod does not carry over */
static void
modsync(Input *in)
{
	if(in->gen != in->bt->gen){
		in->gen = in->bt->gen;
		in->armed = 0;
		in->leaderdown = 0;
	}
}

static int
leaderkey(Input *in)
{
	return in->bt->mode == ModeLeader ? in->bt->leader : -1;
}

/* actions are shorter than ACTLEN, bind_add enforces it */
static void
addact(Kout *out, char *act)
{
	if(out->nact >= MAXOUT)
		return;
	strcpy(out->act[out->nact++], act);
}

/*
 * Chord lookup, normalised for matching only (forwarded messages are never
 * rewritten): the raw rune first, then, with Ctrl held, the control rune
 * mapped back to its ASCII key. The raw rune wins, so under Ctrl a named-key
 * binding shadows its control alias: Mod+BackSpace/Tab/Return/Escape beat
 * Mod+h/i/j/[.
 */
static Binding*
chordlookup(Bindtab *bt, int mods, int held, int r)
{
	Binding *b;
	int n;

	b = bind_lookup(bt, mods, r);
	if(b == nil && (held & MCtl)){
		n = bind_unctl(r);
		if(n != r)
			b = bind_lookup(bt, mods, n);
	}
	return b;
}

static void
swallow(Input *in, Binding *b, int key, Kout *out)
{
	Swal *s;

	if(in->nsw >= MAXHELD)
		return;
	s = &in->sw[in->nsw++];
	s->key = key;
	s->repeat = b->repeat;
	s->cseen = 0;
	strcpy(s->action, b->action);
	addact(out, b->action);
}

static void
unswallow(Input *in, int key)
{
	int i, j;

	j = 0;
	for(i = 0; i < in->nsw; i++)
		if(in->sw[i].key != key)
			in->sw[j++] = in->sw[i];
	in->nsw = j;
}

static Swal*
swalof(Input *in, int key)
{
	int i;

	for(i = 0; i < in->nsw; i++)
		if(in->sw[i].key == key)
			return &in->sw[i];
	return nil;
}

/* forward the held list H minus swallowed keys and the leader key, if it changed */
static void
forward(Input *in, int type, int *h, int nh, Kout *out)
{
	int l[MAXHELD];
	int i, n, same, w, lk;
	char *m;

	lk = leaderkey(in);
	n = 0;
	for(i = 0; i < nh; i++)
		if(h[i] != lk && swalof(in, h[i]) == nil)
			l[n++] = h[i];
	same = n == in->nfwd;
	for(i = 0; same && i < n; i++)
		if(l[i] != in->fwd[i])
			same = 0;
	if(same)
		return;
	memmove(in->fwd, l, n * sizeof l[0]);
	in->nfwd = n;
	m = out->msg;
	m[0] = type;
	w = 1;
	for(i = 0; i < n; i++)
		w += input_utf8enc(l[i], m + w, KMSGLEN - w);
	out->len = w;
}

static void
keydown(Input *in, int *h, int nh, long msec, Kout *out)
{
	Bindtab *bt;
	Binding *b;
	int i, mods, mm, r, lk;

	bt = in->bt;
	mm = bt->modmask;
	lk = leaderkey(in);
	mods = 0;
	for(i = 0; i < nh; i++)
		mods |= bind_modbit(h[i]);
	for(i = 0; i < nh; i++){
		r = h[i];
		if(inlist(in->held, in->nheld, r))
			continue;
		if(!bind_isspecial(r))
			in->last = r;
		if(bt->mode == ModeMask){
			if(bind_isspecial(r))
				continue;
			if((mods & mm) == mm){
				b = chordlookup(bt, (mods & ~mm) | MMod, mods, r);
				if(b != nil)
					swallow(in, b, r, out);
			}
		}else{
			if(r == lk){
				in->leaderdown = 1;
				in->leaderused = 0;
				continue;
			}
			if(in->leaderdown)
				in->leaderused = 1;
			if(in->armed && !bind_isspecial(r)){
				in->armed = 0;
				if(msec - in->armedat >= Leaderms)
					continue;
				b = chordlookup(bt, mods | MMod, mods, r);
				if(b != nil)
					swallow(in, b, r, out);
			}
		}
	}
}

int
input_heldkey(Input *in, int rune)
{
	return inlist(in->held, in->nheld, rune);
}

int
input_modheld(Input *in)
{
	int i, mods;

	modsync(in);
	if(in->bt->mode != ModeMask)
		return in->leaderdown;
	mods = 0;
	for(i = 0; i < in->nheld; i++)
		mods |= bind_modbit(in->held[i]);
	return (mods & in->bt->modmask) == in->bt->modmask;
}

/* a held repeat chord ticks while the WM modifier is held, as niri; a leader chord has none to release */
static int
repeats(Input *in, Swal *s)
{
	return s->repeat == RepRepeat && (in->bt->mode != ModeMask || input_modheld(in));
}

static void
keyup(Input *in, int *h, int nh, long msec)
{
	int i, r, lk;

	lk = leaderkey(in);
	for(i = 0; i < in->nheld; i++){
		r = in->held[i];
		if(inlist(h, nh, r))
			continue;
		unswallow(in, r);
		if(r == in->last)
			in->last = 0;
		if(r == lk && in->leaderdown){
			in->leaderdown = 0;
			if(!in->leaderused){
				in->armed = !(in->armed && msec - in->armedat < Leaderms);
				in->armedat = msec;
			}
		}
	}
}

/*
 * A cooked rune is a swallowed key's own rune, or comes from the last pressed
 * key whatever the rune (Shift: k '-', c '_').
 */
static void
cooked(Input *in, char *msg, int len, int rune, Kout *out)
{
	Swal *s;

	if(rune == leaderkey(in))
		return;
	s = swalof(in, rune);
	if(s == nil)
		s = swalof(in, in->last);
	if(s != nil){
		if(!s->cseen)
			s->cseen = 1;
		else if(repeats(in, s))
			addact(out, s->action);
		return;
	}
	memmove(out->msg, msg, len);
	out->len = len;
}

int
input_kbd(Input *in, char *msg, int len, long msec, Kout *out)
{
	int h[MAXHELD], all[KMSGLEN];
	int nh, na, off, n, r, type, room, i;

	out->len = 0;
	out->nact = 0;
	if(len < 1 || len >= KMSGLEN)
		return -1;
	type = msg[0];
	if(type != 'k' && type != 'K' && type != 'c')
		return -1;
	/* every rune takes at least one byte, so all[] cannot overflow */
	na = 0;
	for(off = 1; off < len; off += n){
		n = input_utf8dec(msg + off, len - off, &r);
		if(n < 0)
			return -1;
		if(r == 0 && type == 'c')
			return -1;
		all[na++] = r;
	}
	if(type == 'c' && na != 1)
		return -1;
	modsync(in);
	/* more than MAXHELD keys: keep every modifier, drop the excess plain keys */
	room = MAXHELD;
	for(i = 0; i < na; i++)
		if(bind_isspecial(all[i]))
			room--;
	nh = 0;
	for(i = 0; i < na && nh < MAXHELD; i++){
		/* a ctl table can yield NUL (Ctrl+2, Ctrl+space): skip it, keep the list */
		if(all[i] == 0)
			continue;
		if(!bind_isspecial(all[i])){
			if(room <= 0)
				continue;
			room--;
		}
		h[nh++] = all[i];
	}
	if(type == 'c'){
		cooked(in, msg, len, h[0], out);
		return 0;
	}
	if(type == 'k'){
		/* kbdfs reports releases with K: a k without a held key means a K was lost, so start over */
		for(i = 0; i < in->nheld; i++)
			if(!inlist(h, nh, in->held[i]))
				break;
		if(i < in->nheld){
			in->nheld = 0;
			in->nsw = 0;
			in->last = 0;
			in->leaderdown = 0;
		}else if(na == in->nheld){
			/* the same list, nothing filtered: whether kbdfs repeats via k, c or both is unrecorded */
			for(i = 0; i < in->nsw; i++)
				if(repeats(in, &in->sw[i]))
					addact(out, in->sw[i].action);
		}
		keydown(in, h, nh, msec, out);
	}else
		keyup(in, h, nh, msec);
	forward(in, type, h, nh, out);
	memmove(in->held, h, nh * sizeof h[0]);
	in->nheld = nh;
	return 0;
}

static int
isstate(char *m)
{
	return m[0] == 'k' || m[0] == 'K';
}

/* the message at position i goes, the newer ones move up */
static void
kqdrop(Kq *q, int i)
{
	int a, b;

	for(; i < q->n - 1; i++){
		a = (q->h + i) % KQ;
		b = (q->h + i + 1) % KQ;
		memmove(q->m[a], q->m[b], q->len[b]);
		q->len[a] = q->len[b];
	}
	q->n--;
}

/*
 * A full queue: k and K are snapshots of the held keys, so the oldest one
 * goes once a newer one is queued or arrives. Else a k, K or z takes the
 * oldest other message's place (z is never dropped), and a c is dropped.
 */
int
kq_put(Kq *q, char *msg, int len)
{
	int i, t, ns, s0, o0;

	if(len + 1 > KMSGLEN)
		return -1;
	if(q->n >= KQ){
		ns = 0;
		s0 = o0 = -1;
		for(i = 0; i < q->n; i++){
			t = (q->h + i) % KQ;
			if(isstate(q->m[t])){
				if(ns++ == 0)
					s0 = i;
			}else if(q->m[t][0] != 'z' && o0 < 0)
				o0 = i;
		}
		if(ns > 1 || (ns == 1 && isstate(msg)))
			kqdrop(q, s0);
		else if((isstate(msg) || msg[0] == 'z') && o0 >= 0)
			kqdrop(q, o0);
		else
			return -1;
	}
	t = (q->h + q->n++) % KQ;
	memmove(q->m[t], msg, len);
	q->m[t][len] = '\0';
	q->len[t] = len + 1;
	return 0;
}

int
kq_get(Kq *q, char *buf, int n)
{
	int l;

	if(q->n == 0)
		return 0;
	l = q->len[q->h];
	if(l > n)
		return -1;
	memmove(buf, q->m[q->h], l);
	q->h = (q->h + 1) % KQ;
	q->n--;
	return l;
}
