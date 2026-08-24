#include "activity.h"

#define HUNDRED_NS_PER_SEC 10000000ull
#define HUNDRED_NS_PER_MS  10000ull

void activity_defaults(ActivityConfig *cfg)
{
    cfg->ch[CH_CPU].threshold = CFG_CPU_DEFAULT;
    cfg->ch[CH_CPU].lo        = CFG_CPU_MIN;
    cfg->ch[CH_CPU].hi        = CFG_CPU_MAX;
    cfg->ch[CH_CPU].multiple  = CFG_CPU_MULTIPLE;
    cfg->ch[CH_CPU].enabled   = 1;

    cfg->ch[CH_DISK].threshold = CFG_DISK_DEFAULT;
    cfg->ch[CH_DISK].lo        = CFG_DISK_MIN;
    cfg->ch[CH_DISK].hi        = CFG_DISK_MAX;
    cfg->ch[CH_DISK].multiple  = CFG_DISK_MULTIPLE;
    cfg->ch[CH_DISK].enabled   = 1;

    cfg->ch[CH_NET].threshold = CFG_NET_DEFAULT;
    cfg->ch[CH_NET].lo        = CFG_NET_MIN;
    cfg->ch[CH_NET].hi        = CFG_NET_MAX;
    cfg->ch[CH_NET].multiple  = CFG_NET_MULTIPLE;
    cfg->ch[CH_NET].enabled   = 1;

    cfg->ch[CH_MEM].threshold = CFG_MEM_DEFAULT;
    cfg->ch[CH_MEM].lo        = CFG_MEM_MIN;
    cfg->ch[CH_MEM].hi        = CFG_MEM_MAX;
    cfg->ch[CH_MEM].multiple  = CFG_MEM_MULTIPLE;
    cfg->ch[CH_MEM].enabled   = 1;

    cfg->wait.threshold = CFG_GRACE_DEFAULT;
    cfg->wait.lo        = CFG_GRACE_MIN;
    cfg->wait.hi        = CFG_GRACE_MAX;
    cfg->wait.multiple  = 1;
    cfg->wait.enabled   = 1;
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
    c->learned_used = 0;
    c->busy = 0;
}

void activity_init(Activity *a, const ActivityConfig *cfg)
{
    int i;
    a->cfg = *cfg;
    a->state = ACT_BUSY;
    a->why = -1;
    a->quiet_ms = 0;
    a->ws_bytes = 0;
    a->conns = 0;
    a->started = 0;
    for (i = 0; i < CH_COUNT; i++) channel_reset(&a->ch[i]);
}

int activity_should_hold(ActivityState s)
{
    return s != ACT_IDLE;
}

const char *activity_channel_name(int c)
{
    switch (c) {
    case CH_CPU:  return "cpu";
    case CH_DISK: return "disk";
    case CH_NET:  return "net";
    case CH_MEM:  return "mem";
    default:      return "-";
    }
}

/* ----------------------------------------------------------- the sliders */

/* The useful range of every one of these spans three or four orders of
   magnitude, so a linear slider would spend its whole travel in the top
   decade and be unusable at the bottom. Position is the logarithm instead,
   done in integers: repeated halving to find the exponent, then linear
   interpolation inside the octave. */

static unsigned int log_scaled(unsigned int v)
{
    unsigned int e = 0, base;
    if (v < 1) v = 1;
    while ((v >> (e + 1)) > 0) e++;      /* floor(log2(v)) */
    base = 1u << e;
    /* 256 steps per octave, so the result is fine-grained enough to be
       reversible without a table. */
    return e * 256 + ((v - base) * 256) / (base ? base : 1);
}

static unsigned int log_unscaled(unsigned int s)
{
    unsigned int e = s / 256;
    unsigned int frac = s % 256;
    unsigned int base;
    if (e > 31) e = 31;
    base = 1u << e;
    return base + (base * frac) / 256;
}

unsigned int activity_to_slider(const ChannelRule *r, unsigned int value)
{
    unsigned int lo = log_scaled(r->lo);
    unsigned int hi = log_scaled(r->hi);
    unsigned int v;

    if (hi <= lo) return 0;
    if (value <= r->lo) return 0;
    if (value >= r->hi) return 1000;

    v = log_scaled(value);
    return ((v - lo) * 1000) / (hi - lo);
}

unsigned int activity_from_slider(const ChannelRule *r, unsigned int pos)
{
    unsigned int lo = log_scaled(r->lo);
    unsigned int hi = log_scaled(r->hi);

    if (pos >= 1000) return r->hi;
    if (pos == 0) return r->lo;
    return log_unscaled(lo + ((hi - lo) * pos) / 1000);
}

/* ---------------------------------------------------------- the channels */

/* Absorbs one sample and decides whether this channel counts as working.

   The bar is where the user put the slider, raised -- never lowered -- if
   this app's own quiet level is higher than that. An Electron IDE that pipes
   megabytes to itself gets a bar above its own noise; a text editor keeps the
   slider's value.

   The learned part is ignored when the long window shows no quiet moment at
   all: an app that has sat at one level throughout has told us nothing about
   its baseline, and treating its current level as the floor is how a video
   export pegged at one core would talk itself into looking idle. */
static void channel_push(Channel *c, unsigned int v, const ChannelRule *rule,
                         int gate)
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

    c->bar = rule->threshold;
    c->learned_used = 0;

    if (c->wlen >= CFG_FLOOR_MIN_SAMPLES && c->high >= c->low * 2) {
        u64 learned = (u64)c->low * rule->multiple;
        if (learned > (u64)c->bar) {
            c->bar = (learned > 0xFFFFFFFFull) ? 0xFFFFFFFFu
                                               : (unsigned int)learned;
            c->learned_used = 1;
        }
    }

    c->busy = rule->enabled && gate && c->smoothed >= c->bar && c->bar > 0;
}

ActivityState activity_update(Activity *a, const TrackerDelta *d, int conns)
{
    u64 rate[CH_COUNT];
    int i;

    a->conns = conns;
    a->ws_bytes = d->ws_bytes;

    if (!a->started) {
        /* The button was just pressed. Hold first, measure afterwards. */
        a->started = 1;
        a->state = ACT_BUSY;
        a->why = -1;
        a->quiet_ms = 0;
        return a->state;
    }

    if (d->d_wall_100ns == 0) return a->state;

    /* Permille of a single core, so the threshold is machine-independent:
       one busy core reads 1000 whether the box has 4 threads or 64. */
    rate[CH_CPU]  = (d->d_cpu_100ns * 1000ull) / d->d_wall_100ns;
    rate[CH_DISK] = (d->d_rw_bytes    * HUNDRED_NS_PER_SEC) / d->d_wall_100ns;
    rate[CH_NET]  = (d->d_other_bytes * HUNDRED_NS_PER_SEC) / d->d_wall_100ns;
    rate[CH_MEM]  = (d->d_faults      * HUNDRED_NS_PER_SEC) / d->d_wall_100ns;

    for (i = 0; i < CH_COUNT; i++) {
        unsigned int v = (rate[i] > 0xFFFFFFFFull) ? 0xFFFFFFFFu
                                                   : (unsigned int)rate[i];
        /* No connection to anywhere means whatever this is, it is not the
           network. */
        int gate = (i == CH_NET) ? (conns > 0) : 1;
        channel_push(&a->ch[i], v, &a->cfg.ch[i], gate);
    }

    a->why = -1;
    for (i = 0; i < CH_COUNT; i++) {
        if (a->ch[i].busy) { a->why = i; break; }
    }

    if (a->why >= 0) {
        a->quiet_ms = 0;
        a->state = ACT_BUSY;
    } else {
        a->quiet_ms += d->d_wall_100ns / HUNDRED_NS_PER_MS;
        a->state = (a->quiet_ms >= a->cfg.wait.threshold) ? ACT_IDLE
                                                           : ACT_GRACE;
    }

    return a->state;
}
