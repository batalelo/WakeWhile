#include "activity.h"

#define HUNDRED_NS_PER_SEC 10000000ull
#define HUNDRED_NS_PER_MS  10000ull

void activity_defaults(ActivityConfig *cfg)
{
    cfg->cpu.absolute  = CFG_CPU_ABSOLUTE;
    cfg->cpu.multiple  = CFG_CPU_MULTIPLE;
    cfg->cpu.margin    = CFG_CPU_MARGIN;

    cfg->disk.absolute = CFG_DISK_ABSOLUTE;
    cfg->disk.multiple = CFG_DISK_MULTIPLE;
    cfg->disk.margin   = CFG_DISK_MARGIN;

    cfg->net.absolute  = CFG_NET_ABSOLUTE;
    cfg->net.multiple  = CFG_NET_MULTIPLE;
    cfg->net.margin    = CFG_NET_MARGIN;

    cfg->grace_ms      = CFG_GRACE_MS;
    cfg->net_enabled   = 1;
}

static void channel_reset(Channel *c)
{
    int i;
    for (i = 0; i < CFG_FLOOR_SAMPLES; i++) c->window[i] = 0;
    for (i = 0; i < CFG_SMOOTH_SAMPLES; i++) c->recent[i] = 0;
    c->wlen = c->wnext = 0;
    c->rlen = c->rnext = 0;
    c->value = c->smoothed = 0;
    c->low = c->high = 0;
    c->bar = 0;
    c->relative_used = 0;
    c->busy = 0;
}

void activity_init(Activity *a, const ActivityConfig *cfg)
{
    a->cfg = *cfg;
    a->state = ACT_BUSY;
    a->why = WHY_FIRST;
    a->quiet_ms = 0;
    a->conns = 0;
    a->started = 0;
    channel_reset(&a->cpu);
    channel_reset(&a->disk);
    channel_reset(&a->net);
}

int activity_should_hold(ActivityState s)
{
    return s != ACT_IDLE;
}

const char *activity_reason_name(BusyReason r)
{
    switch (r) {
    case WHY_CPU:   return "cpu";
    case WHY_DISK:  return "disk";
    case WHY_NET:   return "net";
    case WHY_FIRST: return "start";
    default:        return "-";
    }
}

/* Absorbs one sample and decides whether this channel counts as working.

   The learned bar only applies when the long window shows the app has
   genuinely been quieter at some point -- `high` at least twice `low`. An
   app that has sat at one level throughout has told us nothing about its
   idle baseline, and treating its current level as the floor is how a video
   export pegged at one core would talk itself into looking idle. */
static void channel_push(Channel *c, unsigned int v, const ChannelRule *rule,
                         int enabled)
{
    u64 sum = 0;
    int i;

    c->value = v;

    c->recent[c->rnext] = v;
    c->rnext = (c->rnext + 1) % CFG_SMOOTH_SAMPLES;
    if (c->rlen < CFG_SMOOTH_SAMPLES) c->rlen++;
    for (i = 0; i < c->rlen; i++) sum += c->recent[i];
    c->smoothed = (unsigned int)(sum / (unsigned)c->rlen);

    c->window[c->wnext] = v;
    c->wnext = (c->wnext + 1) % CFG_FLOOR_SAMPLES;
    if (c->wlen < CFG_FLOOR_SAMPLES) c->wlen++;

    c->low = c->window[0];
    c->high = c->window[0];
    for (i = 1; i < c->wlen; i++) {
        if (c->window[i] < c->low)  c->low = c->window[i];
        if (c->window[i] > c->high) c->high = c->window[i];
    }

    c->bar = rule->absolute;
    c->relative_used = 0;

    if (c->wlen >= CFG_FLOOR_MIN_SAMPLES && c->high >= c->low * 2) {
        u64 learned = (u64)c->low * rule->multiple + rule->margin;
        if (learned < (u64)rule->absolute) {
            c->bar = (unsigned int)learned;
            c->relative_used = 1;
        }
    }

    c->busy = enabled && c->smoothed >= c->bar && c->bar > 0;
}

ActivityState activity_update(Activity *a, const TrackerDelta *d, int conns)
{
    u64 cpu_permille, disk_bps, net_bps;

    a->conns = conns;

    if (!a->started) {
        /* The button was just pressed. Hold first, measure afterwards. */
        a->started = 1;
        a->state = ACT_BUSY;
        a->why = WHY_FIRST;
        a->quiet_ms = 0;
        return a->state;
    }

    if (d->d_wall_100ns == 0) return a->state;

    /* Permille of a single core, so the threshold is machine-independent:
       one busy core reads 1000 whether the box has 4 threads or 64. */
    cpu_permille = (d->d_cpu_100ns * 1000ull) / d->d_wall_100ns;
    disk_bps     = (d->d_rw_bytes    * HUNDRED_NS_PER_SEC) / d->d_wall_100ns;
    net_bps      = (d->d_other_bytes * HUNDRED_NS_PER_SEC) / d->d_wall_100ns;

    if (cpu_permille > 0xFFFFFFFFull) cpu_permille = 0xFFFFFFFFull;
    if (disk_bps > 0xFFFFFFFFull)     disk_bps = 0xFFFFFFFFull;
    if (net_bps > 0xFFFFFFFFull)      net_bps = 0xFFFFFFFFull;

    channel_push(&a->cpu,  (unsigned int)cpu_permille, &a->cfg.cpu, 1);
    channel_push(&a->disk, (unsigned int)disk_bps,     &a->cfg.disk, 1);
    /* No connection to anywhere means whatever this is, it is not the
       network. */
    channel_push(&a->net,  (unsigned int)net_bps,      &a->cfg.net,
                 a->cfg.net_enabled && conns > 0);

    if (a->cpu.busy)       a->why = WHY_CPU;
    else if (a->disk.busy) a->why = WHY_DISK;
    else if (a->net.busy)  a->why = WHY_NET;
    else                   a->why = WHY_NOTHING;

    if (a->why != WHY_NOTHING) {
        a->quiet_ms = 0;
        a->state = ACT_BUSY;
    } else {
        a->quiet_ms += d->d_wall_100ns / HUNDRED_NS_PER_MS;
        a->state = (a->quiet_ms >= a->cfg.grace_ms) ? ACT_IDLE : ACT_GRACE;
    }

    return a->state;
}
