/* compat.h: the single portability seam between 9front (6c) and Linux (gcc). */
#ifdef PLAN9
#include <u.h>
#include <libc.h>
#include <regexp.h>
typedef Reprog Rx;
#define lprint print
#define lsnprint snprint
#else
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <regex.h>
typedef regex_t Rx;
#define nil ((void*)0)
typedef unsigned int Rune;
#define lprint printf
#define lsnprint snprintf
#endif
