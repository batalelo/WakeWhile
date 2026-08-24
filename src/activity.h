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

/* The four things watched, in the order they are shown and logged. */
typedef enum {
    CH_CPU  = 0,
    CH_DISK = 1,
    CH_NET  = 2,
    CH_MEM  = 3,
    CH_COUNT = 4
} ChannelId;

typedef struct {
    unsigned int threshold;  /* where the user's slider is                  */
    unsigned int lo, hi;     /* the ends of that slider                     */
    unsigned int multiple;   /* how far above its own quiet level counts    */
    int enabled;
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
    int learned_used;        /* the app's own level raised the bar */
    int busy;
} Channel;

typedef struct {
    ChannelRule ch[CH_COUNT];
    /* How long everything must stay under bar before the lock is dropped.
       Carried as a ChannelRule so it gets a slider like the rest; the
       threshold field is the wait itself, in milliseconds. */
    ChannelRule wait;
} ActivityConfig;

typedef struct {
    ActivityConfig cfg;
    ActivityState  state;
    int            why;      /* a ChannelId, or -1 for nothing */
    u64  quiet_ms;

    Channel ch[CH_COUNT];
    u64  ws_bytes;           /* memory in use across the tree, for display */
    int  conns;              /* established connections off this machine   */
    int  started;
} Activity;

void activity_defaults(ActivityConfig *cfg);
void activity_init(Activity *a, const ActivityConfig *cfg);

/* Folds one tick into the state machine. `conns` is the number of
   established connections to somewhere off this machine; the network channel
   is ignored when it is zero, so an app's local chatter is never mistaken
   for traffic. The very first call reports BUSY so that pressing the button
   engages the lock immediately, and a call covering no elapsed time leaves
   the state untouched. */
ActivityState activity_update(Activity *a, const TrackerDelta *d, int conns);

int activity_should_hold(ActivityState s);

/* "cpu", "disk", "net", "mem", or "-". */
const char *activity_channel_name(int channel_or_minus_one);

/* Where `value` sits between the ends of a slider, 0..1000, on a log scale
   so the low end stays usable. And back again. */
unsigned int activity_to_slider(const ChannelRule *r, unsigned int value);
unsigned int activity_from_slider(const ChannelRule *r, unsigned int pos);

#endif
