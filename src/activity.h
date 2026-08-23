#ifndef NOSLEEP_ACTIVITY_H
#define NOSLEEP_ACTIVITY_H

/* Decides whether the watched app is working, and whether that is enough to
   keep holding the sleep lock.

   Pure: no Win32, no CRT, no allocation. */

#include "config.h"
#include "tracker.h"

typedef enum {
    ACT_BUSY  = 0,  /* working right now                                   */
    ACT_GRACE = 1,  /* quiet, but not for long enough to let go            */
    ACT_IDLE  = 2   /* quiet past the grace window; the lock can be dropped */
} ActivityState;

typedef struct {
    unsigned int cpu_busy_permille;  /* 1000 == one core fully busy */
    unsigned int io_busy_bps;
    unsigned int grace_ms;
} ActivityConfig;

typedef struct {
    ActivityConfig cfg;
    ActivityState  state;
    u64  quiet_ms;       /* consecutive quiet time                    */
    unsigned int cpu_permille;  /* last measurement, for the display  */
    unsigned int io_bps;        /* last measurement, for the display  */
    int  started;
} Activity;

void activity_defaults(ActivityConfig *cfg);
void activity_init(Activity *a, const ActivityConfig *cfg);

/* Folds one tick into the state machine. The very first call reports BUSY so
   that pressing the button engages the lock immediately; a call covering no
   elapsed time leaves the state untouched. */
ActivityState activity_update(Activity *a, const TrackerDelta *d);

/* Whether the lock should be held in this state. */
int activity_should_hold(ActivityState s);

#endif
