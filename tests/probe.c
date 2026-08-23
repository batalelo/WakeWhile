/* An integration probe for the parts the headless tests cannot reach: the
   toolhelp walk, the process tree, GetProcessTimes/GetProcessIoCounters, the
   activity rule on live data, and the sleep lock itself.

       probe <pid> [seconds]

   Prints one line per second and, at the end, whether powercfg would have
   seen a request. Not part of the shipped executable. */

#include <stdio.h>
#include "../src/monitor.c"
#include "../src/tracker.c"
#include "../src/activity.c"
#include "../src/power.c"

static const char *state_name(ActivityState s)
{
    if (s == ACT_BUSY) return "BUSY ";
    if (s == ACT_GRACE) return "grace";
    return "idle ";
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

    pid = 0;
    { const char *p = argv[1]; while (*p >= '0' && *p <= '9') pid = pid * 10 + (unsigned)(*p++ - '0'); }
    if (argc > 2) {
        seconds = 0;
        { const char *p = argv[2]; while (*p >= '0' && *p <= '9') seconds = seconds * 10 + (*p++ - '0'); }
    }
    if (!pid) { printf("bad pid\n"); return 2; }

    if (!monitor_start(&m, pid)) {
        printf("cannot open pid %u\n", pid);
        return 1;
    }

    activity_defaults(&cfg);
    activity_init(&a, &cfg);

    printf("watching pid %u for %d s   (busy at >=%u permille of one core, "
           "or >=%u B/s)\n", pid, seconds, cfg.cpu_busy_permille,
           cfg.io_busy_bps);
    printf("  t  procs   cpu%%core      io B/s   state   quiet_ms  lock\n");

    for (i = 0; i < seconds; i++) {
        TrackerDelta d;
        ActivityState st;
        int hold;

        Sleep(1000);

        if (!monitor_tick(&m, &d)) {
            printf("  process exited\n");
            break;
        }

        st = activity_update(&a, &d);
        hold = activity_should_hold(st);
        power_apply(hold, 0);

        printf("%4d %6d %10u.%u %11u   %s %9u   %s\n",
               i + 1, d.n_procs,
               a.cpu_permille / 10, a.cpu_permille % 10,
               a.io_bps, state_name(st),
               (unsigned)a.quiet_ms,
               hold ? (power_ok() ? "HELD" : "FAILED") : "-");
        fflush(stdout);
    }

    power_release();
    printf("released\n");
    return 0;
}
