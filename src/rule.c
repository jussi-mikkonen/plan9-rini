/* rule.c: window rule table. Pure apart from the regexp seam. */
#include "compat.h"
#include "ctl.h"
#include "action.h"
#include "rule.h"

#ifdef PLAN9
static char rxerr[128];

/* replaces libregexp's regerror, which exits the process */
void
regerror(char *s)
{
	lsnprint(rxerr, sizeof rxerr, "%s", s);
}

static Rx*
rx_comp(char *s, char *err, int nerr)
{
	Rx *p;

	rxerr[0] = '\0';
	p = regcomp(s);
	if(p == nil)
		lsnprint(err, nerr, "bad regexp: %s", rxerr);
	return p;
}

static int
rx_match(Rx *p, char *s)
{
	return regexec(p, s, nil, 0) > 0;
}

static void
rx_free(Rx *p)
{
	free(p);
}
#else
static Rx*
rx_comp(char *s, char *err, int nerr)
{
	char m[128];
	Rx *p;
	int e;

	p = malloc(sizeof *p);
	e = regcomp(p, s, REG_EXTENDED|REG_NOSUB);
	if(e != 0){
		regerror(e, p, m, sizeof m);
		lsnprint(err, nerr, "bad regexp: %s", m);
		free(p);
		return nil;
	}
	return p;
}

static int
rx_match(Rx *p, char *s)
{
	return regexec(p, s, 0, nil, 0) == 0;
}

static void
rx_free(Rx *p)
{
	regfree(p);
	free(p);
}
#endif

static int
parsewidth(char *s, Props *p)
{
	char *sl;
	int n, d;

	if(strlen(s) >= WTEXT)
		return 0;
	sl = strchr(s, '/');
	if(sl == nil){
		if(action_parsenum(s, &n) < 0 || n < 1)
			return 0;
		p->width = n;
		p->widthpx = 1;
	}else{
		*sl = '\0';
		n = d = 0;
		if(action_parsenum(s, &n) < 0 || action_parsenum(sl+1, &d) < 0 || n < 1 || d < n){
			*sl = '/';
			return 0;
		}
		*sl = '/';
		p->width = (1000*n + d/2) / d;
		p->widthpx = 0;
	}
	strcpy(p->wtext, s);
	return 1;
}

static int
parseprops(char **w, int n, Props *p, char *err, int nerr)
{
	int i, j;

	memset(p, 0, sizeof *p);
	for(i = 0; i < n; i++){
		if(strcmp(w[i], "float") == 0)
			p->set = (p->set & ~Ptile) | Pfloat;
		else if(strcmp(w[i], "tile") == 0)
			p->set = (p->set & ~Pfloat) | Ptile;
		else if(strcmp(w[i], "width") == 0){
			if(i+1 >= n){
				lsnprint(err, nerr, "width needs a value");
				return -1;
			}
			if(!parsewidth(w[++i], p)){
				lsnprint(err, nerr, "bad width: %s", w[i]);
				return -1;
			}
			p->set |= Pwidth;
		}else if(strcmp(w[i], "workspace") == 0){
			if(i+1 >= n){
				lsnprint(err, nerr, "workspace needs a number");
				return -1;
			}
			if(action_parsenum(w[++i], &p->ws) < 0 || p->ws < 1){
				lsnprint(err, nerr, "bad workspace: %s", w[i]);
				return -1;
			}
			p->set |= Pws;
		}else if(strcmp(w[i], "rect") == 0){
			if(i+4 >= n){
				lsnprint(err, nerr, "rect needs four numbers");
				return -1;
			}
			for(j = 0; j < 4; j++)
				if(action_parsenum(w[i+1+j], &p->rect[j]) < 0){
					lsnprint(err, nerr, "bad rect: %s", w[i+1+j]);
					return -1;
				}
			i += 4;
			p->set |= Prect;
		}else{
			lsnprint(err, nerr, "bad property: %s", w[i]);
			return -1;
		}
	}
	if(p->set == 0){
		lsnprint(err, nerr, "rule needs a property");
		return -1;
	}
	return 0;
}

int
rule_add(Ruletab *t, char *re, char **props, int nprops, char *err, int nerr)
{
	Props p;
	Rx *rx;
	int i;

	if(strlen(re) >= RULESRC){
		lsnprint(err, nerr, "regexp too long");
		return -1;
	}
	for(i = 0; i < t->n; i++)
		if(strcmp(t->r[i].src, re) == 0)
			break;
	if(i == t->n && t->n >= MAXRULE){
		lsnprint(err, nerr, "too many rules");
		return -1;
	}
	rx = rx_comp(re, err, nerr);
	if(rx == nil)
		return -1;
	if(parseprops(props, nprops, &p, err, nerr) < 0){
		rx_free(rx);
		return -1;
	}
	if(i < t->n)
		rx_free(t->r[i].re);
	else
		t->n++;
	strcpy(t->r[i].src, re);
	t->r[i].re = rx;
	t->r[i].p = p;
	return 0;
}

int
rule_del(Ruletab *t, char *re, char *err, int nerr)
{
	int i;

	for(i = 0; i < t->n; i++)
		if(strcmp(t->r[i].src, re) == 0){
			rx_free(t->r[i].re);
			memmove(&t->r[i], &t->r[i+1], (t->n - i - 1) * sizeof t->r[0]);
			t->n--;
			return 0;
		}
	lsnprint(err, nerr, "no rule %s", re);
	return -1;
}

void
rule_match(Ruletab *t, char *subject, Props *out)
{
	Props *p;
	int i;

	memset(out, 0, sizeof *out);
	for(i = 0; i < t->n; i++){
		if(!rx_match(t->r[i].re, subject))
			continue;
		p = &t->r[i].p;
		if(p->set & (Pfloat|Ptile))
			out->set = (out->set & ~(Pfloat|Ptile)) | (p->set & (Pfloat|Ptile));
		if(p->set & Pwidth){
			out->width = p->width;
			out->widthpx = p->widthpx;
			strcpy(out->wtext, p->wtext);
		}
		if(p->set & Pws)
			out->ws = p->ws;
		if(p->set & Prect)
			memmove(out->rect, p->rect, sizeof out->rect);
		out->set |= p->set & (Pwidth|Pws|Prect);
	}
}

void
rule_fmt(Rule *r, char *buf, int n)
{
	Props *p;
	char q[RULESRC*2+4];
	int o;

	p = &r->p;
	ctl_quote(q, sizeof q, r->src);
	o = lsnprint(buf, n, "rule %s", q);
	if(p->set & Pfloat)
		o += lsnprint(buf+o, n-o, " float");
	if(p->set & Ptile)
		o += lsnprint(buf+o, n-o, " tile");
	if(p->set & Pwidth)
		o += lsnprint(buf+o, n-o, " width %s", p->wtext);
	if(p->set & Pws)
		o += lsnprint(buf+o, n-o, " workspace %d", p->ws);
	if(p->set & Prect)
		lsnprint(buf+o, n-o, " rect %d %d %d %d", p->rect[0], p->rect[1], p->rect[2], p->rect[3]);
}
