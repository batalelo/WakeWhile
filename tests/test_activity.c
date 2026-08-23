/* Headless tests for the two pure modules. Built and run by build.cmd before
   the executable is produced. */

#include <stdio.h>
#include "../src/tracker.c"
#include "../src/activity.c"

#define SEC   10000000ull          /* 100 ns units in one second */
#define BASE  133000000000000000ull  /* a realistic FILETIME instant */
#define CORE  SEC                  /* CPU spent by one core in one second */

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

static ProcSample mk(unsigned int pid, u64 create, u64 cpu, u64 io)
{
    ProcSample s;
    s.pid = pid;
    s.create_100ns = create;
    s.cpu_100ns = cpu;
    s.io_bytes = io;
    return s;
}

/* Drives one tick end to end and returns the resulting state. */
static ActivityState tick(Tracker *t, Activity *a,
                          const ProcSample *snap, int n, u64 now)
{
    TrackerDelta d = tracker_update(t, snap, n, now);
    return activity_update(a, &d);
}

static void begin(Tracker *t, Activity *a)
{
    ActivityConfig cfg;
    activity_defaults(&cfg);
    tracker_reset(t);
    activity_init(a, &cfg);
}

/* ------------------------------------------------------------------ tests */

static void test_first_tick_primes_and_holds(void)
{
    Tracker t; Activity a;
    ProcSample s[1];
    TrackerDelta d;

    begin(&t, &a);
    s[0] = mk(100, BASE, 5 * CORE, 999999);
    d = tracker_update(&t, s, 1, BASE + SEC);

    check_eq(d.d_cpu_100ns, 0, "first snapshot yields no CPU delta");
    check_eq(d.d_io_bytes, 0, "first snapshot yields no I/O delta");
    check_eq(d.d_wall_100ns, 0, "first snapshot yields no elapsed time");
    check(activity_update(&a, &d) == ACT_BUSY,
          "the tick right after the button is pressed holds the lock");
}

static void test_cpu_normalised_to_one_core(void)
{
    Tracker t; Activity a;
    ProcSample s[1];

    /* One core fully busy for one second. */
    begin(&t, &a);
    s[0] = mk(100, BASE, 0, 0);
    tick(&t, &a, s, 1, BASE);
    s[0] = mk(100, BASE, CORE, 0);
    tick(&t, &a, s, 1, BASE + SEC);
    check_eq(a.cpu_permille, 1000, "one busy core over one second reads 1000");

    /* The same rate sampled over five seconds must read the same. */
    begin(&t, &a);
    s[0] = mk(100, BASE, 0, 0);
    tick(&t, &a, s, 1, BASE);
    s[0] = mk(100, BASE, 5 * CORE, 0);
    tick(&t, &a, s, 1, BASE + 5 * SEC);
    check_eq(a.cpu_permille, 1000, "the rate does not depend on tick length");

    /* Four cores fully busy exceed 100%: that is the point of the unit. */
    begin(&t, &a);
    s[0] = mk(100, BASE, 0, 0);
    tick(&t, &a, s, 1, BASE);
    s[0] = mk(100, BASE, 4 * CORE, 0);
    tick(&t, &a, s, 1, BASE + SEC);
    check_eq(a.cpu_permille, 4000, "four busy cores read 4000");
}

static void test_cpu_threshold_boundary(void)
{
    Tracker t; Activity a;
    ProcSample s[1];

    /* 19.9% of a core: idle-app territory, must not hold. */
    begin(&t, &a);
    s[0] = mk(100, BASE, 0, 0);
    tick(&t, &a, s, 1, BASE);
    s[0] = mk(100, BASE, (CORE * 199) / 1000, 0);
    check(tick(&t, &a, s, 1, BASE + SEC) == ACT_GRACE,
          "199 permille is below the threshold");
    check_eq(a.cpu_permille, 199, "199 permille measured");

    /* Exactly 20%: busy. */
    begin(&t, &a);
    s[0] = mk(100, BASE, 0, 0);
    tick(&t, &a, s, 1, BASE);
    s[0] = mk(100, BASE, (CORE * 200) / 1000, 0);
    check(tick(&t, &a, s, 1, BASE + SEC) == ACT_BUSY,
          "200 permille meets the threshold");

    /* 5% -- what an idling Discord or Chrome produces. Deliberately not busy;
       this is why the threshold is not the 5% a task manager would suggest. */
    begin(&t, &a);
    s[0] = mk(100, BASE, 0, 0);
    tick(&t, &a, s, 1, BASE);
    s[0] = mk(100, BASE, (CORE * 50) / 1000, 0);
    check(tick(&t, &a, s, 1, BASE + SEC) == ACT_GRACE,
          "an app idling at 5% of a core does not count as working");
}

static void test_io_counts_as_work(void)
{
    Tracker t; Activity a;
    ProcSample s[1];

    /* A download: no CPU worth speaking of, 4 MB/s of traffic. */
    begin(&t, &a);
    s[0] = mk(100, BASE, 0, 0);
    tick(&t, &a, s, 1, BASE);
    s[0] = mk(100, BASE, CORE / 200, 4ull * 1024 * 1024);
    check(tick(&t, &a, s, 1, BASE + SEC) == ACT_BUSY,
          "4 MB/s of I/O counts as working even at 0.5% CPU");
    check_eq(a.io_bps, 4ull * 1024 * 1024, "I/O rate measured");

    /* Background chatter well under the threshold does not. */
    begin(&t, &a);
    s[0] = mk(100, BASE, 0, 0);
    tick(&t, &a, s, 1, BASE);
    s[0] = mk(100, BASE, 0, 64 * 1024);
    check(tick(&t, &a, s, 1, BASE + SEC) == ACT_GRACE,
          "64 KB/s of I/O is not enough");
}

static void test_grace_window(void)
{
    Tracker t; Activity a;
    ProcSample s[1];
    u64 now = BASE;
    u64 cpu = 0;
    int i;

    begin(&t, &a);
    s[0] = mk(100, BASE, cpu, 0);
    tick(&t, &a, s, 1, now);

    /* One busy second to start the clock. */
    now += SEC; cpu += CORE;
    s[0] = mk(100, BASE, cpu, 0);
    check(tick(&t, &a, s, 1, now) == ACT_BUSY, "busy tick holds");

    /* 59 quiet seconds: still holding. */
    for (i = 0; i < 59; i++) {
        now += SEC;
        s[0] = mk(100, BASE, cpu, 0);
        tick(&t, &a, s, 1, now);
    }
    check(a.state == ACT_GRACE, "59 s of quiet still holds the lock");
    check(activity_should_hold(a.state), "grace means hold");

    /* The 60th tips it over. */
    now += SEC;
    s[0] = mk(100, BASE, cpu, 0);
    check(tick(&t, &a, s, 1, now) == ACT_IDLE,
          "60 s of quiet releases the lock");
    check(!activity_should_hold(ACT_IDLE), "idle means release");

    /* Work resumes: the lock comes straight back. */
    now += SEC; cpu += CORE;
    s[0] = mk(100, BASE, cpu, 0);
    check(tick(&t, &a, s, 1, now) == ACT_BUSY,
          "the lock is re-acquired as soon as work resumes");
    check_eq(a.quiet_ms, 0, "the quiet clock resets");
}

static void test_grace_survives_a_stutter(void)
{
    Tracker t; Activity a;
    ProcSample s[1];
    u64 now = BASE, cpu = 0;
    int round, i;

    begin(&t, &a);
    s[0] = mk(100, BASE, cpu, 0);
    tick(&t, &a, s, 1, now);

    /* A build that works for a second then waits 30 -- over and over. It must
       never release, which is the whole reason the grace window exists. */
    for (round = 0; round < 5; round++) {
        now += SEC; cpu += CORE;
        s[0] = mk(100, BASE, cpu, 0);
        tick(&t, &a, s, 1, now);
        for (i = 0; i < 30; i++) {
            now += SEC;
            s[0] = mk(100, BASE, cpu, 0);
            tick(&t, &a, s, 1, now);
            check(activity_should_hold(a.state),
                  "a 30 s pause inside a job never drops the lock");
        }
    }
}

static void test_child_born_during_the_tick_counts(void)
{
    Tracker t; Activity a;
    ProcSample s[2];
    TrackerDelta d;

    begin(&t, &a);
    s[0] = mk(100, BASE, 0, 0);
    tick(&t, &a, s, 1, BASE);

    /* The parent stays idle; a renderer child spawns half a second in and
       burns a full core. Watching only the parent would report nothing --
       this is the Chrome and Electron case. */
    s[0] = mk(100, BASE, 0, 0);
    s[1] = mk(200, BASE + SEC / 2, CORE / 2, 0);
    d = tracker_update(&t, s, 2, BASE + SEC);

    check_eq(d.d_cpu_100ns, CORE / 2,
             "a child born during the tick contributes all of its CPU");
    check_eq(d.n_procs, 2, "both processes are counted");
    check(activity_update(&a, &d) == ACT_BUSY,
          "work in a child process holds the lock");
}

static void test_preexisting_child_is_adopted_quietly(void)
{
    Tracker t; Activity a;
    ProcSample s[2];
    TrackerDelta d;

    begin(&t, &a);
    s[0] = mk(100, BASE, 0, 0);
    tick(&t, &a, s, 1, BASE);

    /* A long-lived process with hours of CPU behind it joins the tree -- the
       handle finally opened, or it was reparented. Counting its lifetime
       would read as an enormous spike and pin the lock on nothing. */
    s[0] = mk(100, BASE, 0, 0);
    s[1] = mk(300, BASE - 3600ull * SEC, 3000ull * SEC, 999999999);
    d = tracker_update(&t, s, 2, BASE + SEC);

    check_eq(d.d_cpu_100ns, 0,
             "a process that predates the last sample starts from zero");
    check_eq(d.d_io_bytes, 0, "and contributes no I/O for that tick");

    /* From the next tick on it is measured normally. */
    s[1] = mk(300, BASE - 3600ull * SEC, 3000ull * SEC + CORE, 999999999);
    d = tracker_update(&t, s, 2, BASE + 2 * SEC);
    check_eq(d.d_cpu_100ns, CORE, "it is measured normally afterwards");
}

static void test_child_exiting_never_goes_negative(void)
{
    Tracker t; Activity a;
    ProcSample s[2];
    TrackerDelta d;

    begin(&t, &a);
    s[0] = mk(100, BASE, 10 * CORE, 5000);
    s[1] = mk(200, BASE, 40 * CORE, 90000);
    tick(&t, &a, s, 2, BASE);

    /* The heavy child exits. A naive sum of totals would drop by 40 core
       seconds and underflow. */
    s[0] = mk(100, BASE, 10 * CORE, 5000);
    d = tracker_update(&t, s, 1, BASE + SEC);

    check_eq(d.d_cpu_100ns, 0, "a departing child yields no negative CPU");
    check_eq(d.d_io_bytes, 0, "a departing child yields no negative I/O");
    check_eq(d.n_procs, 1, "the tree has shrunk");
    check(activity_update(&a, &d) == ACT_GRACE,
          "and the app is simply quiet, not impossibly busy");
}

static void test_pid_reuse_is_detected(void)
{
    Tracker t; Activity a;
    ProcSample s[1];
    TrackerDelta d;

    begin(&t, &a);
    s[0] = mk(100, BASE, 50 * CORE, 700000);
    tick(&t, &a, s, 1, BASE);

    /* Same PID, different process: it started later and has spent a fraction
       of a second. Matching on PID alone would read the drop as an underflow
       or, worse, as sixty core-seconds of work. */
    s[0] = mk(100, BASE + SEC / 2, CORE / 4, 100);
    d = tracker_update(&t, s, 1, BASE + SEC);

    check_eq(d.d_cpu_100ns, CORE / 4,
             "a recycled PID is treated as the new process it is");
    check_eq(d.d_io_bytes, 100, "its I/O is counted from its own start");
}

static void test_zero_length_tick_is_ignored(void)
{
    Tracker t; Activity a;
    ProcSample s[1];
    TrackerDelta d;
    ActivityState before;

    begin(&t, &a);
    s[0] = mk(100, BASE, 0, 0);
    tick(&t, &a, s, 1, BASE);
    s[0] = mk(100, BASE, CORE, 0);
    before = tick(&t, &a, s, 1, BASE + SEC);

    /* Two samples at the same instant: no elapsed time, so no division and no
       change of mind. */
    d = tracker_update(&t, s, 1, BASE + SEC);
    check_eq(d.d_wall_100ns, 0, "a repeated timestamp elapses nothing");
    check(activity_update(&a, &d) == before,
          "and leaves the state alone");
}

static void test_tree_totals_add_up(void)
{
    Tracker t; Activity a;
    ProcSample s[4];
    TrackerDelta d;
    int i;

    begin(&t, &a);
    for (i = 0; i < 4; i++) s[i] = mk(100 + i, BASE, 0, 0);
    tick(&t, &a, s, 4, BASE);

    /* Four children at a quarter core each: individually below the threshold,
       together exactly at one core. The tree is what we judge. */
    for (i = 0; i < 4; i++) s[i] = mk(100 + i, BASE, CORE / 4, 256 * 1024);
    d = tracker_update(&t, s, 4, BASE + SEC);

    check_eq(d.d_cpu_100ns, CORE, "CPU sums across the tree");
    check_eq(d.d_io_bytes, 1024 * 1024, "I/O sums across the tree");
    check(activity_update(&a, &d) == ACT_BUSY, "the sum crosses the threshold");
    check_eq(a.cpu_permille, 1000, "reported as one full core");
}

static void test_overflow_guard_on_capacity(void)
{
    Tracker t; Activity a;
    static ProcSample s[CFG_MAX_PROCS + 20];
    TrackerDelta d;
    int i;

    begin(&t, &a);
    for (i = 0; i < CFG_MAX_PROCS + 20; i++) s[i] = mk(100 + i, BASE, 0, 0);
    tick(&t, &a, s, CFG_MAX_PROCS + 20, BASE);

    for (i = 0; i < CFG_MAX_PROCS + 20; i++)
        s[i] = mk(100 + i, BASE, CORE / 100, 0);
    d = tracker_update(&t, s, CFG_MAX_PROCS + 20, BASE + SEC);

    /* Only the first CFG_MAX_PROCS are measured, but nothing walks off the
       end of the table. */
    check_eq(d.d_cpu_100ns, (u64)CFG_MAX_PROCS * (CORE / 100),
             "an oversized tree is clamped to the table, not overrun");
    check_eq(d.n_procs, CFG_MAX_PROCS + 20, "the true count is still reported");
}

/* ------------------------------------------------------------------- main */

int main(void)
{
    printf("test_activity\n");

    test_first_tick_primes_and_holds();
    test_cpu_normalised_to_one_core();
    test_cpu_threshold_boundary();
    test_io_counts_as_work();
    test_grace_window();
    test_grace_survives_a_stutter();
    test_child_born_during_the_tick_counts();
    test_preexisting_child_is_adopted_quietly();
    test_child_exiting_never_goes_negative();
    test_pid_reuse_is_detected();
    test_zero_length_tick_is_ignored();
    test_tree_totals_add_up();
    test_overflow_guard_on_capacity();

    if (failures) {
        printf("\n%d of %d checks FAILED\n", failures, checks);
        return 1;
    }
    printf("%d checks passed\n", checks);
    return 0;
}
