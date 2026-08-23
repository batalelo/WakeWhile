#include "tracker.h"

static const ProcSample *find_prev(const Tracker *t, unsigned int pid,
                                   u64 create_100ns)
{
    int i;
    for (i = 0; i < t->count; i++) {
        /* The creation time is what makes this safe: Windows reuses PIDs
           freely, and without the check a recycled PID would look like the
           old process suddenly losing all its accumulated CPU time. */
        if (t->prev[i].pid == pid && t->prev[i].create_100ns == create_100ns)
            return &t->prev[i];
    }
    return 0;
}

void tracker_reset(Tracker *t)
{
    t->count = 0;
    t->last_100ns = 0;
    t->primed = 0;
}

TrackerDelta tracker_update(Tracker *t, const ProcSample *snap, int n,
                            u64 now_100ns)
{
    TrackerDelta d;
    int i;

    d.d_cpu_100ns = 0;
    d.d_rw_bytes = 0;
    d.d_other_bytes = 0;
    d.d_wall_100ns = 0;
    d.n_procs = n;

    if (n > CFG_MAX_PROCS) n = CFG_MAX_PROCS;

    if (t->primed && now_100ns > t->last_100ns)
        d.d_wall_100ns = now_100ns - t->last_100ns;

    for (i = 0; i < n; i++) {
        const ProcSample *cur = &snap[i];
        const ProcSample *old;

        if (!t->primed) continue;

        old = find_prev(t, cur->pid, cur->create_100ns);
        if (old) {
            /* Cumulative counters only ever climb, but clamp anyway rather
               than wrap a u64 into a spurious eternity of activity. */
            if (cur->cpu_100ns > old->cpu_100ns)
                d.d_cpu_100ns += cur->cpu_100ns - old->cpu_100ns;
            if (cur->rw_bytes > old->rw_bytes)
                d.d_rw_bytes += cur->rw_bytes - old->rw_bytes;
            if (cur->other_bytes > old->other_bytes)
                d.d_other_bytes += cur->other_bytes - old->other_bytes;
        } else if (cur->create_100ns > t->last_100ns) {
            /* Born during this tick, so everything it has spent is new. */
            d.d_cpu_100ns += cur->cpu_100ns;
            d.d_rw_bytes += cur->rw_bytes;
            d.d_other_bytes += cur->other_bytes;
        }
        /* Otherwise it predates the last sample but we are only seeing it now
           -- the tree grew sideways, or a handle finally opened. Counting its
           whole lifetime here would read as a huge spike, so we adopt it
           silently and start measuring from the next tick. */
    }

    /* A process that vanished between ticks simply stops contributing. Its
       final sliver of CPU is lost; that is a rounding error, and far better
       than the negative delta a naive sum-of-totals would produce. */

    for (i = 0; i < n; i++) t->prev[i] = snap[i];
    t->count = n;
    t->last_100ns = now_100ns;
    t->primed = 1;

    return d;
}
