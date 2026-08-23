#ifndef NOSLEEP_POWER_H
#define NOSLEEP_POWER_H

/* The sleep lock itself. Everything else in the program exists to decide when
   to call this. */

#include "wincompat.h"

/* Idempotent: only touches the kernel when the request actually changes.
   The request belongs to the calling thread, so this must always be called
   from the thread that runs the message loop. */
void power_apply(int hold, int keep_display);

/* 0 if the last state change was rejected by Windows. */
int power_ok(void);

/* Drops the request. Safe to call when nothing is held. */
void power_release(void);

#endif
