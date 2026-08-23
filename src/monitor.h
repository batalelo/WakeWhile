#ifndef NOSLEEP_MONITOR_H
#define NOSLEEP_MONITOR_H

/* Samples a process and everything it has spawned, and feeds the result to
   the tracker. This is the only place that talks to the kernel about
   processes. */

#include "wincompat.h"
#include "tracker.h"

typedef struct {
    unsigned int root_pid;
    u64          root_create_100ns;
    Tracker      tracker;
    int          alive;

    /* The tree as of the last tick, for whoever needs to ask another
       question about the same processes -- the connection count does. */
    unsigned int pids[CFG_MAX_PROCS];
    int          pid_count;
} Monitor;

/* Returns 0 if the process cannot be opened at all -- gone, or protected. */
int monitor_start(Monitor *m, unsigned int pid);

/* Rebuilds the tree, samples it, and fills `out`. Returns 0 once the root
   process has exited, after which the caller should stop. */
int monitor_tick(Monitor *m, TrackerDelta *out);

#endif
