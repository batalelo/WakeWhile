/* An integration probe for the parts the headless tests cannot reach: the
   toolhelp walk, the process tree, the four channels, the connection count,
   the activity rule on live data, and the sleep lock itself.

       probe <pid> [seconds]

   Prints one line per second. Not part of the shipped executable. */

#include <stdio.h>
#include "../src/monitor.c"
#include "../src/tracker.c"
#include "../src/activity.c"
#include "../src/power.c"
#include "../src/netstat.c"

static const char *state_name(ActivityState s)
{
    if (s == ACT_BUSY) return "BUSY ";
    if (s == ACT_GRACE) return "grace";
    return "IDLE ";
}

static unsigned int parse_uint(const char *p)
{
    unsigned int v = 0;
    while (*p >= '0' && *p <= '9') v = v * 10 + (unsigned)(*p++ - '0');
    return v;
}

int main(int argc, char **argv)
{
    Monitor m;
    Activity a;
    ActivityConfig cfg;
    unsigned int pid;
    int seconds = 10, i;

    if (argc < 2) {
        printf("usage: probe <pid> [seconds]\n");
        return 2;
    }

    pid = parse_uint(argv[1]);
    if (argc > 2) seconds = (int)parse_uint(argv[2]);
    if (!pid) { printf("bad pid\n"); return 2; }

    netstat_init();

    if (!monitor_start(&m, pid)) {
        printf("cannot open pid %u\n", pid);
        return 1;
    }

    activity_defaults(&cfg);
    activity_init(&a, &cfg);

    printf("watching pid %u for %d s\n", pid, seconds);
    printf("thresholds: cpu %u permille of one core | disk %u B/s | "
           "net %u B/s | mem %u faults/s\n",
           cfg.ch[CH_CPU].threshold, cfg.ch[CH_DISK].threshold,
           cfg.ch[CH_NET].threshold, cfg.ch[CH_MEM].threshold);
    printf("a bar marked r was raised by the app's own baseline\n\n");
    printf("   t proc |  cpu   bar |    disk      bar |   net   bar |"
           "    mem     bar |    ws | cn  state  why   quiet\n");

    for (i = 0; i < seconds; i++) {
        TrackerDelta d;
        ActivityState st;
        int conns, hold;

        Sleep(1000);

        if (!monitor_tick(&m, &d)) {
            printf("  process exited\n");
            break;
        }

        conns = netstat_established(m.pids, m.pid_count);
        st = activity_update(&a, &d, conns);
        hold = activity_should_hold(st);
        power_apply(hold, 0);

        printf("%4d %4d | %4u %5u%c | %7u %8u%c | %5u %5u%c | %6u %7u%c |"
               " %4uM | %2d  %s  %-4s %5u\n",
               i + 1, d.n_procs,
               a.ch[CH_CPU].smoothed,  a.ch[CH_CPU].bar,
               a.ch[CH_CPU].learned_used ? 'r' : ' ',
               a.ch[CH_DISK].smoothed, a.ch[CH_DISK].bar,
               a.ch[CH_DISK].learned_used ? 'r' : ' ',
               a.ch[CH_NET].smoothed,  a.ch[CH_NET].bar,
               a.ch[CH_NET].learned_used ? 'r' : ' ',
               a.ch[CH_MEM].smoothed,  a.ch[CH_MEM].bar,
               a.ch[CH_MEM].learned_used ? 'r' : ' ',
               (unsigned)(a.ws_bytes >> 20),
               conns, state_name(st), activity_channel_name(a.why),
               (unsigned)a.quiet_ms);
        fflush(stdout);
    }

    power_release();
    netstat_shutdown();
    printf("released (lock dropped)\n");
    return 0;
}
