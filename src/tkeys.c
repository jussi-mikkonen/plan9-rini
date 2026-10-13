/* tkeys.c: terminal key table, shaped like the chord table in bind.c. */
#include "compat.h"
#include "keys.h"
#include "bind.h"
#include "tkeys.h"

static char *actnames[Nterm] = {
	"insert", "left", "right", "up", "down", "scroll-line-up", "scroll-line-down",
	"page-up", "page-down", "top", "end", "kill-line", "kill-word", "backspace",
	"line-start", "line-end", "output-point", "hold", "return", "interrupt",
	"complete", "tab", "newline"
};

void
tkeys_init(Tkeytab *t)
{
	t->n = 0;
	t->c64 = 1;
}

int
tkeys_action(char *name)
{
	int i;

	for(i = 0; i < Nterm; i++)
		if(strcmp(actnames[i], name) == 0)
			return i;
	return -1;
}

char*
tkeys_actname(int act)
{
	return actnames[act];
}

int
tkeys_add(Tkeytab *t, char *key, char *action, char *err, int nerr)
{
	int mods, k, act, shift, i;

	if(bind_parsechord(key, &mods, &k, err, nerr) < 0)
		return -1;
	if(mods & ~(MShift|MCtl)){
		lsnprint(err, nerr, "bad terminal key: %s", key);
		return -1;
	}
	/* the key as c delivers it */
	if(mods & MCtl){
		if(!((k >= 'a' && k <= 'z') || (k >= '[' && k <= '_'))){
			lsnprint(err, nerr, "bad terminal key: %s", key);
			return -1;
		}
		k &= 0x1F;
		if(k == Kbs || k == '\t' || k == '\n' || k == Kesc){
			lsnprint(err, nerr, "bad terminal key: %s (bind BackSpace, Tab, Return or Escape)", key);
			return -1;
		}
	}else if(mods & MShift){
		if(k >= 'a' && k <= 'z')
			k += 'A' - 'a';
		else if(k > ' ' && k < Kdel){
			lsnprint(err, nerr, "bad terminal key: %s (Shift with a layout-dependent key)", key);
			return -1;
		}
	}
	act = tkeys_action(action);
	if(act < 0){
		lsnprint(err, nerr, "unknown action: %s", action);
		return -1;
	}
	shift = (mods & MShift) != 0;
	for(i = 0; i < t->n; i++)
		if(t->k[i].shift == shift && t->k[i].rune == k)
			break;
	if(i == t->n){
		if(t->n >= MAXTKEY){
			lsnprint(err, nerr, "too many terminal keys");
			return -1;
		}
		t->n++;
	}
	t->k[i].shift = shift;
	t->k[i].rune = k;
	t->k[i].act = act;
	return 0;
}

int
tkeys_lookup(Tkeytab *t, int shift, int rune)
{
	int i, un;

	un = -1;
	for(i = 0; i < t->n; i++){
		if(t->k[i].rune != rune)
			continue;
		if(t->k[i].shift == shift)
			return t->k[i].act;
		if(!t->k[i].shift)
			un = t->k[i].act;
	}
	return un >= 0 ? un : Tinsert;
}

static char *def[][2] = {
	{"Left", "left"}, {"Right", "right"}, {"Up", "up"}, {"Down", "down"},
	{"Shift+Up", "scroll-line-up"}, {"Shift+Down", "scroll-line-down"},
	{"Prior", "page-up"}, {"Next", "page-down"},
	{"Home", "top"}, {"End", "end"},
	{"Ctrl+u", "kill-line"}, {"Ctrl+w", "kill-word"},
	{"BackSpace", "backspace"},
	{"Ctrl+a", "line-start"}, {"Ctrl+e", "line-end"}, {"Ctrl+b", "output-point"},
	{"Escape", "hold"}, {"Return", "return"}, {"Shift+Return", "newline"},
	{"Delete", "interrupt"},
	{"Ctrl+f", "complete"}, {"Insert", "complete"}, {"Tab", "tab"},
	{nil, nil}
};

void
tkeys_defaults(Tkeytab *t)
{
	char err[64];
	int i;

	for(i = 0; def[i][0] != nil; i++)
		tkeys_add(t, def[i][0], def[i][1], err, sizeof err);
}
