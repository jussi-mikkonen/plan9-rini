#include "compat.h"
#include "desk.h"
#include "input.h"
#include "ctl.h"
#include "ev.h"

typedef void (*Emit)(char*, void*);

static Snapwin*
find(Snap *s, int id)
{
	int i;

	for(i = 0; i < s->n; i++)
		if(s->w[i].id == id)
			return &s->w[i];
	return nil;
}

static void
ev1(Emit emit, void *arg, char *verb, int a)
{
	char l[EVLINE];

	lsnprint(l, sizeof l, "%s %d", verb, a);
	emit(l, arg);
}

static void
evlabel(Emit emit, void *arg, char *verb, Snapwin *w)
{
	char l[EVLINE], q[EVLABEL+8];

	ctl_quote(q, sizeof q, w->label);
	lsnprint(l, sizeof l, "%s %d %s", verb, w->id, q);
	emit(l, arg);
}

void
ev_diff(Snap *old, Snap *new, Emit emit, void *arg)
{
	Snapwin *o, *w;
	char l[EVLINE];
	int i, f, of;

	for(i = old->nws; i < new->nws; i++)
		ev1(emit, arg, "wsadd", i+1);
	for(i = old->nws; i > new->nws; i--)
		ev1(emit, arg, "wsdel", i);
	if(old->cur != new->cur)
		ev1(emit, arg, "ws", new->cur+1);
	for(i = 0; i < old->n; i++)
		if(find(new, old->w[i].id) == nil)
			ev1(emit, arg, "close", old->w[i].id);
	for(i = 0; i < new->n; i++){
		w = &new->w[i];
		o = find(old, w->id);
		if(o == nil){
			evlabel(emit, arg, "open", w);
			if(w->kind != Khid && w->ws != new->cur){
				lsnprint(l, sizeof l, "move %d %d", w->id, w->ws+1);
				emit(l, arg);
			}
			if(w->kind == Kfloat)
				ev1(emit, arg, "float", w->id);
			if(w->kind == Khid)
				ev1(emit, arg, "hide", w->id);
			continue;
		}
		if(o->kind != Khid && w->kind == Khid)
			ev1(emit, arg, "hide", w->id);
		else if(o->kind == Khid && w->kind != Khid)
			ev1(emit, arg, "unhide", w->id);
		if(w->kind != Khid && (o->kind == Khid ? w->ws != new->cur : w->ws != o->ws)){
			lsnprint(l, sizeof l, "move %d %d", w->id, w->ws+1);
			emit(l, arg);
		}
		if(o->kind == Khid && w->kind == Kfloat)
			ev1(emit, arg, "float", w->id);
		if(o->kind != Khid && w->kind != Khid && o->kind != w->kind)
			ev1(emit, arg, w->kind == Kfloat ? "float" : "tile", w->id);
		if(strcmp(o->label, w->label) != 0)
			evlabel(emit, arg, "label", w);
	}
	of = old->focus > 0 ? old->focus : 0;
	f = new->focus > 0 ? new->focus : 0;
	if(of != f)
		ev1(emit, arg, "focus", f);
}

void
ev_snapshot(Snap *s, Emit emit, void *arg)
{
	static Snap empty;

	empty.n = 0;
	empty.nws = 0;
	empty.cur = -1;
	empty.focus = -1;
	ev_diff(&empty, s, emit, arg);
}

/* appends whole lines only; returns the new length */
static int
app(char *buf, int n, int o, char *s)
{
	int k;

	k = strlen(s);
	if(o + k >= n)
		return o;
	memmove(buf + o, s, k + 1);
	return o + k;
}

int
ev_statefmt(Snap *s, char *buf, int n)
{
	static char *kind[] = {"tile", "float", "hidden"};
	char q[EVLABEL+8], l[EVLINE];
	Snapwin *w;
	int i, o;

	o = 0;
	buf[0] = '\0';
	lsnprint(l, sizeof l, "workspaces %d current %d\n", s->nws, s->cur+1);
	o = app(buf, n, o, l);
	for(i = 0; i < s->n; i++){
		w = &s->w[i];
		ctl_quote(q, sizeof q, w->label);
		lsnprint(l, sizeof l, "win %d %d %s %d %s\n", w->id, w->kind == Khid ? 0 : w->ws+1, kind[w->kind], w->wmil, q);
		o = app(buf, n, o, l);
	}
	lsnprint(l, sizeof l, "focus %d\n", s->focus > 0 ? s->focus : 0);
	return app(buf, n, o, l);
}

static char ovl[] = "overflow\n";

/* line i of the queue, 0 the head */
static char *
qline(Evq *q, int i)
{
	return q->l[(q->h + i) % NEVQ];
}

void
evq_push(Evq *q, char *line)
{
	char *l;
	int len, k;

	if(q->n == NEVQ){
		/* a partly read head line is finished first, then comes one overflow line */
		k = q->off > 0;
		if(k && strcmp(qline(q, 0), ovl) == 0)
			k = 0;
		else if(strcmp(qline(q, k), ovl) != 0)
			strcpy(qline(q, k), ovl);
		/* drop the oldest line after it */
		for(; k >= 0; k--)
			strcpy(qline(q, k + 1), qline(q, k));
		q->h = (q->h + 1) % NEVQ;
		q->n--;
	}
	len = strlen(line);
	if(len > EVLINE - 1)
		len = EVLINE - 1;
	l = qline(q, q->n++);
	memmove(l, line, len);
	l[len] = '\n';
	l[len + 1] = '\0';
}

int
evq_read(Evq *q, char *buf, int n)
{
	char *l;
	int k;

	if(q->n == 0 || n <= 0)
		return 0;
	l = qline(q, 0) + q->off;
	k = strlen(l);
	if(k > n)
		k = n;
	memmove(buf, l, k);
	if(l[k] != '\0')
		q->off += k;
	else{
		q->h = (q->h + 1) % NEVQ;
		q->n--;
		q->off = 0;
	}
	return k;
}

void
tap_init(Tapq *q)
{
	memset(q, 0, sizeof *q);
	q->lastid = -1;
}

int
tap_push(Tapq *q, int focusid, char *msg, int len)
{
	char z[16];

	if(focusid != q->lastid && kq_put(&q->q, z, lsnprint(z, sizeof z, "z%d", focusid)) == 0)
		q->lastid = focusid;
	return kq_put(&q->q, msg, len);
}

int
tap_read(Tapq *q, char *buf, int n)
{
	int k;

	k = kq_get(&q->q, buf, n);
	if(k > 0 && buf[0] == 'z')
		q->from = atoi(buf + 1);
	return k;
}

int
tap_split(char *d, int n, char **msg, int *len, int max, char *err, int nerr)
{
	int p, e, c;

	if(n < 2){
		lsnprint(err, nerr, "malformed key");
		return -1;
	}
	c = 0;
	for(p = 0; p < n; p = e + 1){
		if(d[p] == '\0'){
			lsnprint(err, nerr, "null message type");
			return -1;
		}
		for(e = p; e < n && d[e] != '\0'; e++)
			;
		if(d[p] != 'z'){
			if(c == max){
				lsnprint(err, nerr, "too many messages");
				return -1;
			}
			msg[c] = d + p;
			len[c] = e - p;
			c++;
		}
	}
	return c;
}
