#ifndef NOSLEEP_TRACKER_H
#define NOSLEEP_TRACKER_H

/* Turns successive snapshots of a process tree into aggregate deltas.

   Pure: no Win32, no CRT, no allocation. All the awkward cases live here --
   children appearing mid-run, children exiting mid-run, and PIDs being reused
   by an unrelated process -- so that they can be tested without a machine to
   put to sleep. */

#include "config.h"

typedef unsigned long long u64;

typedef struct {
    unsigned int pid;
    u64 create_100ns;  /* process creation time; identifies this incarnation */
    u64 cpu_100ns;     /* kernel + user, cumulative since the process started */
    u64 io_bytes;      /* read + write + other, cumulative                    */
} ProcSample;

typedef struct {
    u64 d_cpu_100ns;
    u64 d_io_bytes;
    u64 d_wall_100ns;
    int n_procs;       /* processes in the tree at this instant */
} TrackerDelta;

typedef struct {
    ProcSample prev[CFG_MAX_PROCS];
    int  count;
    u64  last_100ns;
    int  primed;       /* 0 until the first snapshot has been absorbed */
} Tracker;

void tracker_reset(Tracker *t);

/* Absorbs `snap` (n entries, taken at `now_100ns`) and returns the aggregate
   change since the previous call. The first call establishes the baseline and
   returns all zeros. */
TrackerDelta tracker_update(Tracker *t, const ProcSample *snap, int n,
                            u64 now_100ns);

#endif
