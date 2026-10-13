/* action.h: action strings, the one vocabulary for bindings and ctl. */
#ifndef ACTION_H
#define ACTION_H

#include "desk.h"

#define ACTLEN 128

/* returns 0 on success, -1 with a message in err */
int action_run(Desk*, char *act, char *err, int nerr);

/* shared by the actions and the test driver; 0 ok, -1 bad */
int action_parsecenter(char *arg, int *mode);
char *action_centername(int mode);	/* never, always, on-overflow */
/* optional sign then digits only; values beyond 1000000 are rejected */
int action_parsenum(char *s, int *out);

#endif
