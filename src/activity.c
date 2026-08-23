#include "activity.h"

#define HUNDRED_NS_PER_SEC 10000000ull
#define HUNDRED_NS_PER_MS  10000ull

void activity_defaults(ActivityConfig *cfg)
{
    cfg->cpu_busy_permille = CFG_CPU_BUSY_PERMILLE;
    cfg->io_busy_bps       = CFG_IO_BUSY_BPS;
    cfg->grace_ms          = CFG_GRACE_MS;
}

void activity_init(Activity *a, const ActivityConfig *cfg)
{
    a->cfg = *cfg;
    a->state = ACT_BUSY;
    a->quiet_ms = 0;
    a->cpu_permille = 0;
    a->io_bps = 0;
    a->started = 0;
}

int activity_should_hold(ActivityState s)
{
    return s != ACT_IDLE;
}

ActivityState activity_update(Activity *a, const TrackerDelta *d)
{
    u64 cpu_permille, io_bps;
    int busy;

    if (!a->started) {
        /* The button was just pressed. Hold first, measure afterwards. */
        a->started = 1;
        a->state = ACT_BUSY;
        a->quiet_ms = 0;
        return a->state;
    }

    if (d->d_wall_100ns == 0) return a->state;

    /* Permille of a single core, so the threshold is machine-independent:
       one busy core reads 1000 whether the box has 4 threads or 64. */
    cpu_permille = (d->d_cpu_100ns * 1000ull) / d->d_wall_100ns;

    /* Bytes per second. d_io_bytes is capped in practice by disk bandwidth,
       so the multiply cannot come near overflowing a u64. */
    io_bps = (d->d_io_bytes * HUNDRED_NS_PER_SEC) / d->d_wall_100ns;

    a->cpu_permille = (cpu_permille > 0xFFFFFFFFull)
                          ? 0xFFFFFFFFu : (unsigned int)cpu_permille;
    a->io_bps = (io_bps > 0xFFFFFFFFull)
                    ? 0xFFFFFFFFu : (unsigned int)io_bps;

    busy = (cpu_permille >= a->cfg.cpu_busy_permille) ||
           (io_bps >= a->cfg.io_busy_bps);

    if (busy) {
        a->quiet_ms = 0;
        a->state = ACT_BUSY;
    } else {
        a->quiet_ms += d->d_wall_100ns / HUNDRED_NS_PER_MS;
        a->state = (a->quiet_ms >= a->cfg.grace_ms) ? ACT_IDLE : ACT_GRACE;
    }

    return a->state;
}
