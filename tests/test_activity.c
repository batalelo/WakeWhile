/* Headless tests for the two pure modules. Built and run by build.cmd before
   the executable is produced. */

#include <stdio.h>
#include "../src/tracker.c"
#include "../src/activity.c"

#define SEC   10000000ull            /* 100 ns units in one second */
#define BASE  133000000000000000ull  /* a realistic FILETIME instant */
#define CORE  SEC                    /* CPU spent by one core in one second */

static int failures = 0;
static int checks = 0;

static void check(int ok, const char *what)
{
    checks++;
    if (!ok) {
        failures++;
        printf("  FAIL  %s\n", what);
    }
}

static void check_eq(u64 got, u64 want, const char *what)
{
    checks++;
    if (got != want) {
        failures++;
        printf("  FAIL  %s: got %llu, want %llu\n", what, got, want);
    }
}

/* ---------------------------------------------------------------- helpers */

static ProcSample mk(unsigned int pid, u64 create, u64 cpu, u64 rw, u64 other)
{
    ProcSample s;
    s.pid = pid;
    s.create_100ns = create;
    s.cpu_100ns = cpu;
    s.rw_bytes = rw;
    s.other_bytes = other;
    return s;
}

/* One second's worth of the given rates. */
static TrackerDelta rates(unsigned int cpu_permille, u64 rw_bps, u64 other_bps)
{
    TrackerDelta d;
    d.d_wall_100ns = SEC;
    d.d_cpu_100ns = (u64)cpu_permille * (SEC / 1000);
    d.d_rw_bytes = rw_bps;
    d.d_other_bytes = other_bps;
    d.n_procs = 1;
    return d;
}

static void begin(Activity *a)
{
    ActivityConfig cfg;
    activity_defaults(&cfg);
    activity_init(a, &cfg);
    /* Consume the free BUSY tick that engages the lock on the button press. */
    {
        TrackerDelta zero = rates(0, 0, 0);
        zero.d_wall_100ns = 0;
        activity_update(a, &zero, 0);
    }
}

static ActivityState run(Activity *a, int seconds, unsigned int cpu,
                         u64 rw, u64 other, int conns)
{
    ActivityState s = a->state;
    int i;
    for (i = 0; i < seconds; i++) {
        TrackerDelta d = rates(cpu, rw, other);
        s = activity_update(a, &d, conns);
    }
    return s;
}

/* ---------------------------------------------------------- tracker tests */

static void test_first_snapshot_is_only_a_baseline(void)
{
    Tracker t;
    ProcSample s[1];
    TrackerDelta d;

    tracker_reset(&t);
    s[0] = mk(100, BASE, 5 * CORE, 999999, 4242);
    d = tracker_update(&t, s, 1, BASE + SEC);

    check_eq(d.d_cpu_100ns, 0, "first snapshot yields no CPU delta");
    check_eq(d.d_rw_bytes, 0, "first snapshot yields no read/write delta");
    check_eq(d.d_other_bytes, 0, "first snapshot yields no other delta");
    check_eq(d.d_wall_100ns, 0, "first snapshot yields no elapsed time");
}

static void test_channels_stay_separate(void)
{
    Tracker t;
    ProcSample s[1];
    TrackerDelta d;

    tracker_reset(&t);
    s[0] = mk(100, BASE, 0, 0, 0);
    tracker_update(&t, s, 1, BASE);

    /* An Electron app pipes megabytes between its own windows while its
       socket traffic is a trickle. Lumping the two together is precisely
       what made an idle IDE look permanently busy. */
    s[0] = mk(100, BASE, 0, 2300000, 700);
    d = tracker_update(&t, s, 1, BASE + SEC);

    check_eq(d.d_rw_bytes, 2300000, "read/write is reported on its own");
    check_eq(d.d_other_bytes, 700, "socket traffic is reported on its own");
}

static void test_child_born_during_the_tick_counts(void)
{
    Tracker t;
    ProcSample s[2];
    TrackerDelta d;

    tracker_reset(&t);
    s[0] = mk(100, BASE, 0, 0, 0);
    tracker_update(&t, s, 1, BASE);

    /* The parent stays idle; a renderer child spawns half a second in and
       burns a full core. Watching only the parent would report nothing --
       this is the Chrome and Electron case. */
    s[0] = mk(100, BASE, 0, 0, 0);
    s[1] = mk(200, BASE + SEC / 2, CORE / 2, 0, 0);
    d = tracker_update(&t, s, 2, BASE + SEC);

    check_eq(d.d_cpu_100ns, CORE / 2,
             "a child born during the tick contributes all of its CPU");
    check_eq(d.n_procs, 2, "both processes are counted");
}

static void test_preexisting_child_is_adopted_quietly(void)
{
    Tracker t;
    ProcSample s[2];
    TrackerDelta d;

    tracker_reset(&t);
    s[0] = mk(100, BASE, 0, 0, 0);
    tracker_update(&t, s, 1, BASE);

    /* A long-lived process with hours of CPU behind it joins the tree.
       Counting its lifetime would read as an enormous spike. */
    s[0] = mk(100, BASE, 0, 0, 0);
    s[1] = mk(300, BASE - 3600ull * SEC, 3000ull * SEC, 999999999, 88888);
    d = tracker_update(&t, s, 2, BASE + SEC);

    check_eq(d.d_cpu_100ns, 0,
             "a process that predates the last sample starts from zero");
    check_eq(d.d_rw_bytes, 0, "and contributes no read/write for that tick");
    check_eq(d.d_other_bytes, 0, "and contributes no socket bytes either");

    s[1] = mk(300, BASE - 3600ull * SEC, 3000ull * SEC + CORE, 999999999, 88888);
    d = tracker_update(&t, s, 2, BASE + 2 * SEC);
    check_eq(d.d_cpu_100ns, CORE, "it is measured normally afterwards");
}

static void test_child_exiting_never_goes_negative(void)
{
    Tracker t;
    ProcSample s[2];
    TrackerDelta d;

    tracker_reset(&t);
    s[0] = mk(100, BASE, 10 * CORE, 5000, 10);
    s[1] = mk(200, BASE, 40 * CORE, 90000, 999);
    tracker_update(&t, s, 2, BASE);

    s[0] = mk(100, BASE, 10 * CORE, 5000, 10);
    d = tracker_update(&t, s, 1, BASE + SEC);

    check_eq(d.d_cpu_100ns, 0, "a departing child yields no negative CPU");
    check_eq(d.d_rw_bytes, 0, "a departing child yields no negative read/write");
    check_eq(d.d_other_bytes, 0, "a departing child yields no negative other");
    check_eq(d.n_procs, 1, "the tree has shrunk");
}

static void test_pid_reuse_is_detected(void)
{
    Tracker t;
    ProcSample s[1];
    TrackerDelta d;

    tracker_reset(&t);
    s[0] = mk(100, BASE, 50 * CORE, 700000, 500);
    tracker_update(&t, s, 1, BASE);

    /* Same PID, different process. Matching on PID alone would read the drop
       as an underflow, or as sixty core-seconds of work. */
    s[0] = mk(100, BASE + SEC / 2, CORE / 4, 100, 7);
    d = tracker_update(&t, s, 1, BASE + SEC);

    check_eq(d.d_cpu_100ns, CORE / 4,
             "a recycled PID is treated as the new process it is");
    check_eq(d.d_rw_bytes, 100, "its I/O is counted from its own start");
}

static void test_tree_totals_add_up(void)
{
    Tracker t;
    ProcSample s[4];
    TrackerDelta d;
    int i;

    tracker_reset(&t);
    for (i = 0; i < 4; i++) s[i] = mk(100 + i, BASE, 0, 0, 0);
    tracker_update(&t, s, 4, BASE);

    for (i = 0; i < 4; i++)
        s[i] = mk(100 + i, BASE, CORE / 4, 256 * 1024, 1000);
    d = tracker_update(&t, s, 4, BASE + SEC);

    check_eq(d.d_cpu_100ns, CORE, "CPU sums across the tree");
    check_eq(d.d_rw_bytes, 1024 * 1024, "read/write sums across the tree");
    check_eq(d.d_other_bytes, 4000, "socket bytes sum across the tree");
}

static void test_oversized_tree_is_clamped(void)
{
    Tracker t;
    static ProcSample s[CFG_MAX_PROCS + 20];
    TrackerDelta d;
    int i;

    tracker_reset(&t);
    for (i = 0; i < CFG_MAX_PROCS + 20; i++) s[i] = mk(100 + i, BASE, 0, 0, 0);
    tracker_update(&t, s, CFG_MAX_PROCS + 20, BASE);

    for (i = 0; i < CFG_MAX_PROCS + 20; i++)
        s[i] = mk(100 + i, BASE, CORE / 100, 0, 0);
    d = tracker_update(&t, s, CFG_MAX_PROCS + 20, BASE + SEC);

    check_eq(d.d_cpu_100ns, (u64)CFG_MAX_PROCS * (CORE / 100),
             "an oversized tree is clamped to the table, not overrun");
    check_eq(d.n_procs, CFG_MAX_PROCS + 20, "the true count is still reported");
}

/* --------------------------------------------------------- activity tests */

static void test_first_tick_holds(void)
{
    Activity a;
    ActivityConfig cfg;
    TrackerDelta d = rates(0, 0, 0);

    activity_defaults(&cfg);
    activity_init(&a, &cfg);
    d.d_wall_100ns = 0;

    check(activity_update(&a, &d, 0) == ACT_BUSY,
          "the tick right after the button is pressed holds the lock");
    check(a.why == -1, "and says so");
}

static void test_cpu_normalised_to_one_core(void)
{
    Activity a;

    begin(&a);
    run(&a, 1, 1000, 0, 0, 0);
    check_eq(a.ch[CH_CPU].value, 1000, "one busy core reads 1000 permille");

    begin(&a);
    run(&a, 1, 4000, 0, 0, 0);
    check_eq(a.ch[CH_CPU].value, 4000, "four busy cores read 4000");
}

static void test_a_single_spike_is_not_work(void)
{
    Activity a;
    TrackerDelta d;

    begin(&a);
    run(&a, 20, 50, 0, 0, 0);          /* quiet */
    d = rates(1200, 0, 0);             /* one wild second */
    activity_update(&a, &d, 0);

    check(a.state != ACT_BUSY,
          "one spike inside a quiet run does not count as work");
    check(a.ch[CH_CPU].smoothed < a.ch[CH_CPU].bar, "the short mean absorbs it");
}

static void test_sustained_cpu_is_work(void)
{
    Activity a;

    begin(&a);
    check(run(&a, 10, 1000, 0, 0, 0) == ACT_BUSY,
          "a core held busy for ten seconds is work");
    check(a.why == CH_CPU, "and the reason given is the CPU");
}

static void test_grace_window(void)
{
    Activity a;

    begin(&a);
    run(&a, 5, 1000, 0, 0, 0);
    check(a.state == ACT_BUSY, "busy while working");

    /* Work stops. The short mean takes a few seconds to fall below the bar,
       and only then does the grace window start running -- so the lock is
       released a little over a minute after the last real work, not exactly
       on it. */
    run(&a, 60, 0, 0, 0, 0);
    check(a.state == ACT_GRACE, "a minute after work stops it is still held");
    check(activity_should_hold(a.state), "grace means hold");

    run(&a, 8, 0, 0, 0, 0);
    check(a.state == ACT_IDLE, "and a few seconds later it is released");
    check(!activity_should_hold(ACT_IDLE), "idle means release");

    run(&a, 5, 1000, 0, 0, 0);
    check(a.state == ACT_BUSY, "the lock comes back when work resumes");
    check_eq(a.quiet_ms, 0, "and the quiet clock resets");
}

static void test_pauses_inside_a_job_never_release(void)
{
    Activity a;
    int round;

    begin(&a);
    for (round = 0; round < 5; round++) {
        run(&a, 2, 1500, 0, 0, 0);
        run(&a, 30, 0, 0, 0, 0);
        check(activity_should_hold(a.state),
              "a 30 s pause inside a job never drops the lock");
    }
}

/* The bug this whole design exists for: an IDE that pipes megabytes a second
   between its own processes and burns a quarter of a core while the user is
   not touching it. Measured on VS Code: 2.3 MB/s of read/write, CPU wandering
   between 6% and 63% of a core, sockets almost silent. Under fixed absolute
   thresholds the lock was never released. */
static void test_an_idle_ide_is_allowed_to_sleep(void)
{
    Activity a;
    int i;
    static const unsigned int cpu_trace[12] = {
        139, 216, 200, 416, 290, 152, 630, 216, 247, 108, 486, 92
    };

    begin(&a);
    for (i = 0; i < 200; i++) {
        TrackerDelta d = rates(cpu_trace[i % 12],
                               2300000 + (i % 7) * 9000,   /* ~2.3 MB/s */
                               660 + (i % 5) * 30);        /* ~0.7 KB/s     */
        activity_update(&a, &d, 4);
    }

    check(a.state == ACT_IDLE,
          "an idle IDE reaches idle despite 2 MB/s of internal piping");
    check(!a.ch[CH_DISK].learned_used,
          "its piping never lets up, so no baseline can be learned from it");
    check_eq(a.ch[CH_DISK].bar, CFG_DISK_DEFAULT,
             "and the slider value is what it is judged against");
    check(a.ch[CH_DISK].smoothed < a.ch[CH_DISK].bar, "and its piping stayed under the bar");
}

/* An app whose noise genuinely swings gets its bar lifted, so that its own
   loud-but-ordinary moments are not mistaken for work. The lift only ever
   raises the bar; it can never make the app easier to call busy. */
static void test_a_swinging_baseline_raises_the_bar(void)
{
    Activity a;
    int i;

    begin(&a);
    for (i = 0; i < 120; i++) {
        /* Between 2.3 and 6 MB/s: quiet enough sometimes to learn from. */
        TrackerDelta d = rates(0, (i % 2) ? 6000000 : 2300000, 0);
        activity_update(&a, &d, 0);
    }

    check(a.ch[CH_DISK].learned_used, "the disk bar was raised to suit it");
    check(a.ch[CH_DISK].bar > CFG_DISK_DEFAULT, "above where the slider sits");
    check(a.state != ACT_BUSY, "so its own churn does not read as work");
}

/* The other half: the same noisy IDE, now running an AI API session -- small
   bursts of network traffic every few seconds and nothing else to show for
   it. The lock must never be released. */
static void test_network_bursts_inside_a_noisy_ide_hold_the_lock(void)
{
    Activity a;
    int cycle, i;

    begin(&a);
    /* Settle, so the baseline is learned from genuinely quiet seconds. */
    for (i = 0; i < 40; i++) {
        TrackerDelta d = rates(200, 2050000, 680);
        activity_update(&a, &d, 4);
    }

    for (cycle = 0; cycle < 6; cycle++) {
        /* 8 s of API traffic: measured at roughly 3.7 KB/s on the wire. */
        for (i = 0; i < 8; i++) {
            TrackerDelta d = rates(200, 2050000, 680 + 3700);
            activity_update(&a, &d, 4);
        }
        check(a.state == ACT_BUSY, "a network burst is recognised as work");
        check(a.why == CH_NET, "and attributed to the network");

        /* 10 s of waiting on the model. */
        for (i = 0; i < 10; i++) {
            TrackerDelta d = rates(200, 2050000, 680);
            activity_update(&a, &d, 4);
        }
        check(activity_should_hold(a.state),
              "the gap between bursts never reaches the grace window");
    }
}

static void test_network_needs_a_connection(void)
{
    Activity a;

    /* The same byte rate with nothing open cannot be network traffic; it is
       the app talking to itself through a device. */
    begin(&a);
    run(&a, 40, 0, 0, 0, 0);
    run(&a, 10, 0, 0, 60000, 0);
    check(a.state != ACT_BUSY, "socket bytes with no connection are ignored");

    begin(&a);
    run(&a, 40, 0, 0, 0, 2);
    run(&a, 10, 0, 0, 60000, 2);
    check(a.state == ACT_BUSY, "the same bytes with a connection count");
    check(a.why == CH_NET, "as network work");
}

/* An export pegged at one core from the first second to the last has never
   shown us a quiet moment. Treating its own busy level as its baseline would
   let it talk itself into looking idle, and the machine would sleep in the
   middle of the job. */
static void test_a_flat_out_job_is_never_mistaken_for_idle(void)
{
    Activity a;
    int i;

    begin(&a);
    for (i = 0; i < 200; i++) {
        TrackerDelta d = rates(1000 + (unsigned)(i % 3), 0, 0);
        activity_update(&a, &d, 0);
    }

    check(a.state == ACT_BUSY, "a flat-out job stays busy indefinitely");
    check(!a.ch[CH_CPU].learned_used,
          "and its own level is refused as a baseline");
    check_eq(a.ch[CH_CPU].bar, CFG_CPU_DEFAULT, "so the slider value stands");
}

static void test_a_quiet_app_keeps_the_sensitive_bars(void)
{
    Activity a;
    int i;

    begin(&a);
    for (i = 0; i < 40; i++) {
        TrackerDelta d = rates(0, 0, 0);
        activity_update(&a, &d, 1);
    }

    check(!a.ch[CH_CPU].learned_used,
          "a quiet app gives its baseline no reason to raise anything");
    check_eq(a.ch[CH_CPU].bar,  CFG_CPU_DEFAULT,  "the CPU bar is the slider");
    check_eq(a.ch[CH_DISK].bar, CFG_DISK_DEFAULT, "and so is the disk bar");
    check_eq(a.ch[CH_NET].bar,  CFG_NET_DEFAULT,  "and the network bar");

    /* Which means modest work on a quiet app is still caught. */
    run(&a, 6, 700, 0, 0, 1);
    check(a.state == ACT_BUSY,
          "70% of one core is work for an app that does nothing otherwise");
}

static void test_zero_length_tick_changes_nothing(void)
{
    Activity a;
    TrackerDelta d;
    ActivityState before;

    begin(&a);
    before = run(&a, 5, 1000, 0, 0, 0);

    d = rates(0, 0, 0);
    d.d_wall_100ns = 0;
    check(activity_update(&a, &d, 0) == before,
          "a tick covering no time leaves the state alone");
}

static void test_reason_names(void)
{
    check(activity_channel_name(CH_CPU)[0] == 'c', "cpu names itself");
    check(activity_channel_name(CH_DISK)[0] == 'd', "disk names itself");
    check(activity_channel_name(CH_NET)[0] == 'n', "net names itself");
    check(activity_channel_name(CH_MEM)[0] == 0x6d, "mem names itself");
    check(activity_channel_name(-1)[0] == 0x2d, "and quiet is a dash");
}

/* ------------------------------------------------------------------- main */

int main(void)
{
    printf("test_activity\n");

    test_first_snapshot_is_only_a_baseline();
    test_channels_stay_separate();
    test_child_born_during_the_tick_counts();
    test_preexisting_child_is_adopted_quietly();
    test_child_exiting_never_goes_negative();
    test_pid_reuse_is_detected();
    test_tree_totals_add_up();
    test_oversized_tree_is_clamped();

    test_first_tick_holds();
    test_cpu_normalised_to_one_core();
    test_a_single_spike_is_not_work();
    test_sustained_cpu_is_work();
    test_grace_window();
    test_pauses_inside_a_job_never_release();
    test_an_idle_ide_is_allowed_to_sleep();
    test_a_swinging_baseline_raises_the_bar();
    test_network_bursts_inside_a_noisy_ide_hold_the_lock();
    test_network_needs_a_connection();
    test_a_flat_out_job_is_never_mistaken_for_idle();
    test_a_quiet_app_keeps_the_sensitive_bars();
    test_zero_length_tick_changes_nothing();
    test_reason_names();

    if (failures) {
        printf("\n%d of %d checks FAILED\n", failures, checks);
        return 1;
    }
    printf("%d checks passed\n", checks);
    return 0;
}
