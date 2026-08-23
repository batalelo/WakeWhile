/* An integration probe for the parts the headless tests cannot reach: the
   toolhelp walk, the process tree, the three I/O channels, the connection
   count, the activity rule on live data, and the sleep lock itself.

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
    printf("each channel: the lower of an absolute bar, or this app's own "
           "recent low x mult + margin\n");
    printf("  cpu  abs %u permille  rel low*%u+%u\n",
           cfg.cpu.absolute, cfg.cpu.multiple, cfg.cpu.margin);
    printf("  disk abs %u B/s  rel low*%u+%u\n",
           cfg.disk.absolute, cfg.disk.multiple, cfg.disk.margin);
    printf("  net  abs %u B/s  rel low*%u+%u  (only with a connection open)\n",
           cfg.net.absolute, cfg.net.multiple, cfg.net.margin);
    printf("\n");
    printf("   t proc |  cpu  smth   bar |    disk    smth      bar |"
           "    net   smth    bar | cn  state  why   quiet\n");

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

        printf("%4d %4d | %4u %5u %5u%c | %7u %7u %8u%c | %6u %6u %6u%c |"
               " %2d  %s  %-5s %5u\n",
               i + 1, d.n_procs,
               a.cpu.value, a.cpu.smoothed, a.cpu.bar,
               a.cpu.relative_used ? 'r' : ' ',
               a.disk.value, a.disk.smoothed, a.disk.bar,
               a.disk.relative_used ? 'r' : ' ',
               a.net.value, a.net.smoothed, a.net.bar,
               a.net.relative_used ? 'r' : ' ',
               conns, state_name(st), activity_reason_name(a.why),
               (unsigned)a.quiet_ms);
        fflush(stdout);
    }

    power_release();
    netstat_shutdown();
    printf("released (lock dropped)\n");
    return 0;
}
