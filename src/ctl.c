/* ctl.c: ctl line parser, quoter and read summary (pure). */
#include "compat.h"
#include "ctl.h"

static int
blank(int c)
{
	return c == ' ' || c == '\t' || c == '\n';
}

char *
ctl_skip(char *p)
{
	while(blank(*p))
		p++;
	return p;
}

static int
wordlen(char *p)
{
	int n;

	for(n = 0; p[n] != '\0' && !blank(p[n]); n++)
		;
	return n;
}

char *
ctl_word(char **pp)
{
	char *p, *w;

	w = ctl_skip(*pp);
	if(*w == '\0'){
		*pp = w;
		return nil;
	}
	p = w + wordlen(w);
	if(*p != '\0')
		*p++ = '\0';
	*pp = p;
	return w;
}

int
ctl_tokenize(char *s, char **tok, int max, char *err, int nerr)
{
	char *in, *out;
	int n, c;

	in = s;
	n = 0;
	for(;;){
		in = ctl_skip(in);
		if(*in == '\0')
			break;
		if(n == max){
			lsnprint(err, nerr, "too many words");
			return -1;
		}
		out = in;
		tok[n++] = out;
		while(*in != '\0' && !blank(*in)){
			if(*in != '\''){
				*out++ = *in++;
				continue;
			}
			in++;
			for(;;){
				c = *in++;
				if(c == '\0'){
					lsnprint(err, nerr, "unterminated quote");
					return -1;
				}
				if(c == '\'' && *in != '\'')
					break;
				if(c == '\'')
					in++;
				*out++ = c;
			}
		}
		c = *in;
		*out = '\0';
		if(c != '\0')
			in++;
	}
	return n;
}

int
ctl_quote(char *buf, int n, char *s)
{
	unsigned char *p;
	int o, q, c;

	if(n <= 0)
		return 0;
	q = *s == '\0';
	for(p = (unsigned char*)s; *p != '\0'; p++)
		if(*p <= ' ' || *p == 0x7f || *p == '\'')
			q = 1;
	o = 0;
	if(q && o < n-1)
		buf[o++] = '\'';
	for(p = (unsigned char*)s; *p != '\0'; p++){
		c = *p;
		if(c < ' ' || c == 0x7f)
			c = ' ';
		if(c == '\'' && o < n-1)
			buf[o++] = '\'';
		if(o < n-1)
			buf[o++] = c;
	}
	if(q && o < n-1)
		buf[o++] = '\'';
	buf[o] = '\0';
	return o;
}

int
ctl_parsecolour(char *s, unsigned long *rgba, char *err, int nerr)
{
	/* strspn first: strtoul alone would take spaces, a sign or a second 0x */
	if(strlen(s) == 10 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')
	&& strspn(s+2, "0123456789abcdefABCDEF") == 8){
		*rgba = strtoul(s+2, nil, 16);
		return 0;
	}
	lsnprint(err, nerr, "bad colour: %s (want 0xRRGGBBAA)", s);
	return -1;
}

/* indexed by the C* enum; Caction is the not-found index */
static char *verbs[] = {"bind", "unbind", "termbind", "set", "rule", "unrule", "spawn", nil};

/* indexed by the S* enum */
static char *settings[] = {"mod", "center", "c64return", "background", nil};

static int
parseset(char *p, Ctlcmd *c, char *err, int nerr)
{
	char *name, *val;
	int i;

	name = ctl_word(&p);
	for(i = 0; name != nil && settings[i] != nil; i++)
		if(strcmp(name, settings[i]) == 0)
			break;
	if(name != nil && settings[i] == nil){
		lsnprint(err, nerr, "unknown setting: %s", name);
		return -1;
	}
	/* mod takes the rest of the line, so "mod leader F12" round-trips */
	if(name != nil && i == Smod){
		val = ctl_skip(p);
		for(p = val + strlen(val); p > val && blank(p[-1]); p--)
			;
		*p = '\0';
		if(*val == '\0')
			val = nil;
	}else
		val = ctl_word(&p);
	if(val == nil){
		lsnprint(err, nerr, "set: want name value");
		return -1;
	}
	c->set = i;
	c->arg[0] = val;
	c->narg = 1;
	if(i == Sc64return && strcmp(val, "on") != 0 && strcmp(val, "off") != 0){
		lsnprint(err, nerr, "c64return: want on or off");
		return -1;
	}
	if(i == Sbackground)
		return ctl_parsecolour(val, &c->colour, err, nerr);
	return 0;
}

static int
parsebind(char *p, Ctlcmd *c, char *err, int nerr)
{
	int n;

	c->arg[0] = ctl_word(&p);
	c->arg[1] = "once";
	c->narg = 2;
	p = ctl_skip(p);
	n = wordlen(p);
	if((n == 4 && strncmp(p, "once", 4) == 0) || (n == 6 && strncmp(p, "repeat", 6) == 0)){
		c->arg[1] = p;
		p += n;
		if(*p != '\0')
			*p++ = '\0';
		p = ctl_skip(p);
	}
	c->rest = p;
	if(c->arg[0] == nil || *p == '\0'){
		lsnprint(err, nerr, "bind: want chord [once|repeat] action");
		return -1;
	}
	return 0;
}

/* the regexp verbs: tokens are the arguments, at least min and, for unrule, exactly min */
static int
parserule(char *p, Ctlcmd *c, int min, char *msg, char *err, int nerr)
{
	int k;

	k = ctl_tokenize(p, c->arg, CTLMAXARG, err, nerr);
	if(k < 0)
		return -1;
	c->narg = k;
	if(k < min || (c->verb == Cunrule && k != min)){
		lsnprint(err, nerr, "%s", msg);
		return -1;
	}
	return 0;
}

int
ctl_parse(char *line, Ctlcmd *c, char *err, int nerr)
{
	char *p;
	int i, n;

	memset(c, 0, sizeof *c);
	if(strlen(line) >= CTLLINE){
		lsnprint(err, nerr, "line too long");
		return -1;
	}
	line = ctl_skip(line);
	n = wordlen(line);
	p = line + n;
	for(i = 0; verbs[i] != nil; i++)
		if(strncmp(line, verbs[i], n) == 0 && verbs[i][n] == '\0')
			break;
	c->verb = i;
	switch(i){
	case Cbind:
		return parsebind(p, c, err, nerr);
	case Cunbind:
		c->narg = 1;
		c->arg[0] = ctl_word(&p);
		if(c->arg[0] == nil || ctl_word(&p) != nil){
			lsnprint(err, nerr, "unbind: want chord");
			return -1;
		}
		return 0;
	case Ctermbind:
		c->narg = 2;
		c->arg[0] = ctl_word(&p);
		c->arg[1] = ctl_word(&p);
		if(c->arg[1] == nil || ctl_word(&p) != nil){
			lsnprint(err, nerr, "termbind: want key action");
			return -1;
		}
		return 0;
	case Cset:
		return parseset(p, c, err, nerr);
	case Crule:
		return parserule(p, c, 2, "rule: want regexp props", err, nerr);
	case Cunrule:
		return parserule(p, c, 1, "unrule: want regexp", err, nerr);
	case Cspawn:
		c->rest = ctl_skip(p);
		return 0;
	}
	c->rest = line;
	return 0;
}

int
ctl_summary(char *buf, int n, char *mod, char *center, int c64, unsigned long bg, int focus, int ws)
{
	return lsnprint(buf, n, "mod %s\ncenter %s\nc64return %s\nbackground 0x%08lX\nfocus %d\nws %d\n",
		mod, center, c64 ? "on" : "off", bg, focus, ws);
}
