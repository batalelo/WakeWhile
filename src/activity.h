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

/* Which signal decided it. Reported so the log can answer "why is my machine
   still awake" without anyone having to guess. */
typedef enum {
    WHY_NOTHING = 0,
    WHY_CPU     = 1,
    WHY_DISK    = 2,
    WHY_NET     = 3,
    WHY_FIRST   = 4   /* the tick right after the button was pressed */
} BusyReason;

/* The two bars a channel can be judged against. */
typedef struct {
    unsigned int absolute;
    unsigned int multiple;
    unsigned int margin;
} ChannelRule;

typedef struct {
    /* Long window, for this app's own quiet level. */
    unsigned int window[CFG_FLOOR_SAMPLES];
    int wlen, wnext;
    /* Short window, so one stray spike cannot count as work. */
    unsigned int recent[CFG_SMOOTH_SAMPLES];
    int rlen, rnext;

    unsigned int value;      /* the raw rate this tick        */
    unsigned int smoothed;   /* mean of the short window      */
    unsigned int low;        /* quietest in the long window   */
    unsigned int high;       /* busiest in the long window    */
    unsigned int bar;        /* what `smoothed` had to beat   */
    int relative_used;       /* whether the learned bar applied */
    int busy;
} Channel;

typedef struct {
    ChannelRule cpu;
    ChannelRule disk;
    ChannelRule net;
    unsigned int grace_ms;
    int net_enabled;
} ActivityConfig;

typedef struct {
    ActivityConfig cfg;
    ActivityState  state;
    BusyReason     why;
    u64  quiet_ms;

    Channel cpu;    /* permille of one core */
    Channel disk;   /* bytes per second     */
    Channel net;    /* bytes per second     */

    int conns;      /* established connections off this machine */
    int started;
} Activity;

void activity_defaults(ActivityConfig *cfg);
void activity_init(Activity *a, const ActivityConfig *cfg);

/* Folds one tick into the state machine. `conns` is the number of
   established connections to somewhere off this machine; the network signal
   is ignored when it is zero, so an app's local chatter is never mistaken
   for traffic. The very first call reports BUSY so that pressing the button
   engages the lock immediately, and a call covering no elapsed time leaves
   the state untouched. */
ActivityState activity_update(Activity *a, const TrackerDelta *d, int conns);

int activity_should_hold(ActivityState s);
const char *activity_reason_name(BusyReason r);

#endif
