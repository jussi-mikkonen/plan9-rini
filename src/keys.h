/* keys.h: keyboard constants. On Plan 9 the system header is used; on Linux
 * the values below mirror 9front /sys/include/keyboard.h (fetched 2026-10-06). */
#ifndef KEYS_H
#define KEYS_H

#ifdef PLAN9
#include <keyboard.h>
#else
enum {
	KF = 0xF000,
	Spec = 0xF800,

	Kshift = KF|0x16,
	Kctl = KF|0x17,
	Kalt = KF|0x15,
	Kaltgr = Spec|0x67,
	Kmod4 = Spec|0x68,

	Kleft = KF|0x11,
	Kright = KF|0x12,
	Kup = KF|0x0E,
	Kdown = Spec|0x00,
	Khome = KF|0x0D,
	Kend = KF|0x18,
	Kpgup = KF|0x0F,
	Kpgdown = KF|0x13,
	Kins = KF|0x14,

	Kesc = 0x1B,
	Kdel = 0x7F,
	Kbs = 0x08
};
#endif

#endif
