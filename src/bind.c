/* bind.c: binding table, chord and modifier parsing, defaults. */
#include "compat.h"
#include "keys.h"
#include "bind.h"

typedef struct Kname Kname;
struct Kname {
	char *name;
	int val;	/* a rune, or a modifier bit in modnames */
};

/* in chord text order, see bind_fmt */
static Kname modnames[] = {
	{"Mod", MMod}, {"Mod4", MMod4}, {"Ctrl", MCtl}, {"Alt", MAlt},
	{"Shift", MShift},
	{nil, 0}
};

static Kname knames[] = {
	{"minus", '-'}, {"plus", '+'}, {"equal", '='}, {"comma", ','},
	{"period", '.'}, {"space", ' '}, {"Return", '\n'}, {"Tab", '\t'},
	{"Escape", Kesc}, {"BackSpace", Kbs}, {"Delete", Kdel},
	{"Left", Kleft}, {"Right", Kright}, {"Up", Kup}, {"Down", Kdown},
	{"Home", Khome}, {"End", Kend}, {"Prior", Kpgup}, {"Next", Kpgdown},
	{"Insert", Kins},
	{"F1", KF|1}, {"F2", KF|2}, {"F3", KF|3}, {"F4", KF|4},
	{"F5", KF|5}, {"F6", KF|6}, {"F7", KF|7}, {"F8", KF|8},
	{"F9", KF|9}, {"F10", KF|10}, {"F11", KF|11}, {"F12", KF|12},
	{nil, 0}
};

int
bind_modbit(int rune)
{
	switch(rune){
	case Kshift:
		return MShift;
	case Kctl:
		return MCtl;
	case Kalt:
		return MAlt;
	case Kmod4:
		return MMod4;
	}
	return 0;
}

static int
modname(char *name)
{
	Kname *k;

	for(k = modnames; k->name != nil; k++)
		if(strcmp(k->name, name) == 0)
			return k->val;
	return 0;
}

static int
keyname(char *name)
{
	Kname *k;

	if(name[0] == '\0')
		return -1;
	if(name[1] == '\0'){
		if(name[0] >= 'A' && name[0] <= 'Z')
			return -1;
		if(name[0] >= 0x21 && name[0] <= 0x7E)
			return name[0];
		return -1;
	}
	for(k = knames; k->name != nil; k++)
		if(strcmp(k->name, name) == 0)
			return k->val;
	return -1;
}

int
bind_isspecial(int r)
{
	return bind_modbit(r) != 0 || r == Kaltgr;
}

void
bind_init(Bindtab *bt)
{
	bt->n = 0;
	bt->leader = 0;
	bt->mode = ModeMask;
	bt->modmask = MMod4;
	bt->gen = 0;
}

/*
 * Which physical key produces U+001B..U+001F (or U+0000) depends on the ctl
 * table kbdfs uses (kbtabctl or table 4 of the loaded kbmap), so punctuation
 * chords under Ctrl+Alt are best-effort.
 */
static int ctlpunct[] = { '[', '\\', ']', '^', '_' };

int
bind_unctl(int r)
{
	if(r >= 1 && r <= 26)
		return r - 1 + 'a';
	if(r == 0)
		return '@';
	if(r >= 0x1B && r <= 0x1F)
		return ctlpunct[r - 0x1B];
	return r;
}

/* split s in place on '+'; returns token count or -1 if too many */
static int
split(char *s, char **tok, int max)
{
	int n;

	n = 0;
	for(;;){
		if(n >= max)
			return -1;
		tok[n++] = s;
		while(*s != '+' && *s != '\0')
			s++;
		if(*s == '\0')
			break;
		*s++ = '\0';
	}
	return n;
}

/* copy s to buf (ACTLEN) and split it; -1 with err if too long, too many parts or AltGr */
static int
toks(char *s, char *buf, char **tok, char *what, char *err, int nerr)
{
	int i, n;

	n = strlen(s);
	if(n >= ACTLEN){
		lsnprint(err, nerr, "%s too long", what);
		return -1;
	}
	memmove(buf, s, n + 1);
	n = split(buf, tok, 8);
	if(n < 0){
		lsnprint(err, nerr, "bad %s", what);
		return -1;
	}
	for(i = 0; i < n; i++)
		if(strcmp(tok[i], "AltGr") == 0){
			lsnprint(err, nerr, "AltGr not allowed");
			return -1;
		}
	return n;
}

int
bind_setmod(Bindtab *bt, char *spec, char *err, int nerr)
{
	char buf[ACTLEN];
	char *tok[8];
	int i, n, m, mask, r;

	if(strncmp(spec, "leader ", 7) == 0){
		r = keyname(spec + 7);
		if(r < 0){
			lsnprint(err, nerr, "unknown key: %s", spec + 7);
			return -1;
		}
		if(bind_isspecial(r) || r <= Kdel){
			lsnprint(err, nerr, "leader key must not be a modifier, printable or control key");
			return -1;
		}
		bt->mode = ModeLeader;
		bt->leader = r;
		bt->gen++;
		return 0;
	}
	n = toks(spec, buf, tok, "modifier spec", err, nerr);
	if(n < 0)
		return -1;
	mask = 0;
	for(i = 0; i < n; i++){
		m = modname(tok[i]);
		if(m == MShift){
			/* Mod+x and Mod+Shift+x would resolve to the same mask */
			lsnprint(err, nerr, "Shift not allowed in modifier");
			return -1;
		}
		if(m == 0 || m == MMod){
			lsnprint(err, nerr, "unknown modifier: %s", tok[i]);
			return -1;
		}
		mask |= m;
	}
	if(mask == MAlt){
		lsnprint(err, nerr, "bare Alt not allowed");
		return -1;
	}
	if(!(mask & MMod4) && (mask & (MCtl|MAlt)) != (MCtl|MAlt)){
		lsnprint(err, nerr, "modifier must include Mod4 or be Ctrl+Alt");
		return -1;
	}
	bt->mode = ModeMask;
	bt->modmask = mask;
	bt->gen++;
	return 0;
}

int
bind_parsechord(char *chord, int *mods, int *key, char *err, int nerr)
{
	char buf[ACTLEN];
	char *tok[8];
	int i, n, m, k, bit;

	n = toks(chord, buf, tok, "chord", err, nerr);
	if(n < 0)
		return -1;
	m = 0;
	k = -1;
	for(i = 0; i < n; i++){
		if(tok[i][0] == '\0'){
			lsnprint(err, nerr, "bad chord");
			return -1;
		}
		bit = modname(tok[i]);
		if(bit != 0){
			if(k >= 0){
				lsnprint(err, nerr, "key must be last in chord");
				return -1;
			}
			m |= bit;
			continue;
		}
		if(k >= 0){
			lsnprint(err, nerr, "chord has more than one key");
			return -1;
		}
		k = keyname(tok[i]);
		if(k < 0){
			lsnprint(err, nerr, "unknown key: %s", tok[i]);
			return -1;
		}
	}
	if(k < 0){
		lsnprint(err, nerr, "chord needs a key");
		return -1;
	}
	*mods = m;
	*key = k;
	return 0;
}

static int
findbind(Bindtab *bt, int mods, int key)
{
	int i;

	for(i = 0; i < bt->n; i++)
		if(bt->b[i].mods == mods && bt->b[i].key == key)
			return i;
	return -1;
}

int
bind_add(Bindtab *bt, char *chord, char *repeat, char *action, char *err, int nerr)
{
	int mods, key, rep, i, len;
	Binding *b;

	if(bind_parsechord(chord, &mods, &key, err, nerr) < 0)
		return -1;
	if(!(mods & MMod)){
		lsnprint(err, nerr, "chord must include Mod: %s", chord);
		return -1;
	}
	if(strcmp(repeat, "once") == 0)
		rep = RepOnce;
	else if(strcmp(repeat, "repeat") == 0)
		rep = RepRepeat;
	else{
		lsnprint(err, nerr, "bad repeat policy: %s", repeat);
		return -1;
	}
	len = strlen(action);
	if(len == 0){
		lsnprint(err, nerr, "empty action");
		return -1;
	}
	if(len >= ACTLEN){
		lsnprint(err, nerr, "action too long");
		return -1;
	}
	i = findbind(bt, mods, key);
	if(i >= 0)
		b = &bt->b[i];
	else if(bt->n < MAXBIND)
		b = &bt->b[bt->n++];
	else{
		lsnprint(err, nerr, "too many bindings");
		return -1;
	}
	b->mods = mods;
	b->key = key;
	b->repeat = rep;
	memmove(b->action, action, len + 1);
	return 0;
}

/*
 * Exact: the router passes Mod plus the held modifiers outside the mask, so
 * a binding naming a key of the mask (Mod+Ctrl+h under Ctrl+Alt) never fires.
 */
Binding*
bind_lookup(Bindtab *bt, int mods, int key)
{
	int i;

	i = findbind(bt, mods, key);
	return i < 0 ? nil : &bt->b[i];
}

/* the name of key in chord text; one holds a single-rune name */
static char *
keytext(int key, char *one)
{
	Kname *k;

	for(k = knames; k->name != nil; k++)
		if(k->val == key)
			return k->name;
	one[0] = key;
	one[1] = '\0';
	return one;
}

/* modifier names in mask joined by '+' */
static void
modtext(int mask, char *t)
{
	Kname *k;

	t[0] = '\0';
	for(k = modnames; k->name != nil; k++)
		if(mask & k->val){
			if(t[0] != '\0')
				strcat(t, "+");
			strcat(t, k->name);
		}
}

void
bind_fmt(Binding *b, char *buf, int n)
{
	char one[2];
	char t[ACTLEN];

	modtext(b->mods, t);
	lsnprint(buf, n, "%s%s%s", t, t[0] != '\0' ? "+" : "", keytext(b->key, one));
}

void
bind_line(Binding *b, char *buf, int n)
{
	char chord[ACTLEN];

	bind_fmt(b, chord, sizeof chord);
	lsnprint(buf, n, "bind %s %s %s", chord, b->repeat == RepRepeat ? "repeat" : "once", b->action);
}

/* the inverse of bind_setmod */
void
bind_modfmt(Bindtab *bt, char *buf, int n)
{
	char one[2];
	char t[ACTLEN];

	if(bt->mode == ModeLeader){
		lsnprint(buf, n, "leader %s", keytext(bt->leader, one));
		return;
	}
	modtext(bt->modmask, t);
	lsnprint(buf, n, "%s", t);
}

int
bind_del(Bindtab *bt, char *chord, char *err, int nerr)
{
	int mods, key, i;

	if(bind_parsechord(chord, &mods, &key, err, nerr) < 0)
		return -1;
	i = findbind(bt, mods, key);
	if(i < 0){
		lsnprint(err, nerr, "not bound: %s", chord);
		return -1;
	}
	memmove(&bt->b[i], &bt->b[i + 1], (bt->n - i - 1) * sizeof(Binding));
	bt->n--;
	return 0;
}

void
bind_defaults(Bindtab *bt)
{
	static char *def[][3] = {
		{"Mod+t", "once", "spawn"},
		{"Mod+Return", "once", "spawn"},
		{"Mod+q", "once", "delete"},
		{"Mod+h", "repeat", "focus-column-left"},
		{"Mod+Left", "repeat", "focus-column-left"},
		{"Mod+l", "repeat", "focus-column-right"},
		{"Mod+Right", "repeat", "focus-column-right"},
		{"Mod+Home", "once", "focus-column-first"},
		{"Mod+End", "once", "focus-column-last"},
		{"Mod+r", "once", "switch-preset-column-width"},
		{"Mod+Shift+r", "once", "switch-preset-column-width-back"},
		{"Mod+minus", "repeat", "set-column-width -10%"},
		{"Mod+plus", "repeat", "set-column-width +10%"},
		{"Mod+equal", "repeat", "set-column-width +10%"},
		{"Mod+n", "repeat", "set-column-width -10%"},
		{"Mod+w", "repeat", "set-column-width +10%"},
		{"Mod+c", "once", "center-column"},
		{"Mod+Shift+e", "once", "quit"},
		{"Mod+F1", "once", "show-hotkey-overlay"},
		/* niri model; no key for consume-or-expel-window-left/right, no Mod+Shift+digit */
		{"Mod+j", "repeat", "focus-window-or-workspace-down"},
		{"Mod+Down", "repeat", "focus-window-or-workspace-down"},
		{"Mod+k", "repeat", "focus-window-or-workspace-up"},
		{"Mod+Up", "repeat", "focus-window-or-workspace-up"},
		{"Mod+Ctrl+h", "repeat", "move-column-left"},
		{"Mod+Ctrl+Left", "repeat", "move-column-left"},
		{"Mod+Ctrl+l", "repeat", "move-column-right"},
		{"Mod+Ctrl+Right", "repeat", "move-column-right"},
		{"Mod+Ctrl+j", "repeat", "move-window-down-or-to-workspace-down"},
		{"Mod+Ctrl+Down", "repeat", "move-window-down-or-to-workspace-down"},
		{"Mod+Ctrl+k", "repeat", "move-window-up-or-to-workspace-up"},
		{"Mod+Ctrl+Up", "repeat", "move-window-up-or-to-workspace-up"},
		{"Mod+comma", "once", "consume-window-into-column"},
		{"Mod+period", "once", "expel-window-from-column"},
		{"Mod+u", "once", "focus-workspace-down"},
		{"Mod+Next", "once", "focus-workspace-down"},
		{"Mod+i", "once", "focus-workspace-up"},
		{"Mod+Prior", "once", "focus-workspace-up"},
		{"Mod+Ctrl+u", "once", "move-column-to-workspace-down"},
		{"Mod+Ctrl+Next", "once", "move-column-to-workspace-down"},
		{"Mod+Ctrl+i", "once", "move-column-to-workspace-up"},
		{"Mod+Ctrl+Prior", "once", "move-column-to-workspace-up"},
		{"Mod+1", "once", "focus-workspace 1"},
		{"Mod+2", "once", "focus-workspace 2"},
		{"Mod+3", "once", "focus-workspace 3"},
		{"Mod+4", "once", "focus-workspace 4"},
		{"Mod+5", "once", "focus-workspace 5"},
		{"Mod+6", "once", "focus-workspace 6"},
		{"Mod+7", "once", "focus-workspace 7"},
		{"Mod+8", "once", "focus-workspace 8"},
		{"Mod+9", "once", "focus-workspace 9"},
		{"Mod+Ctrl+1", "once", "move-column-to-workspace 1"},
		{"Mod+Ctrl+2", "once", "move-column-to-workspace 2"},
		{"Mod+Ctrl+3", "once", "move-column-to-workspace 3"},
		{"Mod+Ctrl+4", "once", "move-column-to-workspace 4"},
		{"Mod+Ctrl+5", "once", "move-column-to-workspace 5"},
		{"Mod+Ctrl+6", "once", "move-column-to-workspace 6"},
		{"Mod+Ctrl+7", "once", "move-column-to-workspace 7"},
		{"Mod+Ctrl+8", "once", "move-column-to-workspace 8"},
		{"Mod+Ctrl+9", "once", "move-column-to-workspace 9"},
		{"Mod+v", "once", "toggle-window-floating"},
		{"Mod+Shift+v", "once", "switch-focus-between-floating-and-tiling"},
		{"Mod+f", "once", "maximize-column"},
		{"Mod+Shift+f", "once", "fullscreen-window"},
		{"Mod+Ctrl+r", "once", "switch-preset-window-height"},
		{"Mod+Ctrl+Shift+r", "once", "reset-window-height"},
		{"Mod+Shift+minus", "repeat", "set-window-height -10%"},
		{"Mod+Shift+plus", "repeat", "set-window-height +10%"},
		{"Mod+Shift+equal", "repeat", "set-window-height +10%"},
		{nil, nil, nil}
	};
	char err[64];
	int i;

	for(i = 0; def[i][0] != nil; i++)
		bind_add(bt, def[i][0], def[i][1], def[i][2], err, sizeof err);
}
