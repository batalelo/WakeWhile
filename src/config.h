#ifndef NOSLEEP_CONFIG_H
#define NOSLEEP_CONFIG_H

/* Tunables live here and nowhere else.
   Rationale, and the measurements behind the numbers, are in
   docs/superpowers/specs/2026-08-24-activity-rule-revision.md. */

/* ---------------------------------------------------------------- sampling */

#define CFG_POLL_MS            1000u

/* A burst of work and a one-second blip look the same at a single sample, so
   every channel is judged on a short rolling mean instead. Five seconds is
   long enough to ignore a stray spike and short enough to notice a stream of
   API responses arriving every few seconds. */
#define CFG_SMOOTH_SAMPLES     5

/* How far back we look for an app's own quiet level. */
#define CFG_FLOOR_SAMPLES      180        /* 3 minutes */
#define CFG_FLOOR_MIN_SAMPLES  30

/* ------------------------------------------------------------- the channels

   Four things are watched, and each is judged against a threshold the user
   can move. The threshold is a floor, not the whole story: an app noisier
   than the default has its own baseline learned and the bar rises to suit,
   but it never drops below where the slider is set.

     bar = max( slider , this app's recent quiet level x multiple )

   Defaults come from measurement -- see the spec. They sit above what an idle
   Electron IDE does and below anything that counts as work. */

/* CPU, in permille of ONE core: 1000 = one core pegged. Machine-independent
   by construction, unlike the figure a task manager shows.
   Idle VS Code wanders up to 630 on a single sample, about 330 smoothed. */
#define CFG_CPU_DEFAULT        500u
#define CFG_CPU_MIN            50u
#define CFG_CPU_MAX            4000u
#define CFG_CPU_MULTIPLE       3u

/* ReadFile and WriteFile. Named pipes land here too, which is why an idle
   Electron IDE can read megabytes a second: its windows talk this way. */
#define CFG_DISK_DEFAULT       4194304u   /* 4 MB/s   */
#define CFG_DISK_MIN           65536u     /* 64 KB/s  */
#define CFG_DISK_MAX           268435456u /* 256 MB/s */
#define CFG_DISK_MULTIPLE      2u

/* Sockets. Windows will not give an unprivileged process a per-process
   network byte count, but socket traffic goes through DeviceIoControl and so
   lands in the "other" counter -- measured, a 358 KB/s download showed up as
   331 KB/s of "other" with read and write both flat at zero.
   Idle VS Code chatters at 1-5.5 KB/s: sync, telemetry, extension polling,
   and a real Claude Code session runs at a median of 1 KB/s with peaks to
   11 KB/s -- the two populations overlap almost completely. What separates
   them is not the level but how long the gaps are. Measured over 240 s of a
   real session, the longest stretch with everything under bar was 37 s at
   this threshold; over idle VS Code it was 234 s. Raising it to 4 KB/s turns
   the session gap into 73 s and the machine sleeps mid-task. */
#define CFG_NET_DEFAULT        2048u      /* 2 KB/s */
#define CFG_NET_MIN            512u
#define CFG_NET_MAX            4194304u   /* 4 MB/s  */
#define CFG_NET_MULTIPLE       2u

/* Memory, as page faults per second. The working set on its own says nothing
   about activity -- an app holding 2 GB and doing nothing is still doing
   nothing -- but a process that is really working touches new pages
   constantly. */
#define CFG_MEM_DEFAULT        20000u     /* faults/s */
#define CFG_MEM_MIN            500u
#define CFG_MEM_MAX            2000000u
#define CFG_MEM_MULTIPLE       3u

/* --------------------------------------------------------------- releasing */

/* A single quiet tick must not drop the lock: real workloads pause on I/O,
   on the network, and between build stages. */
#define CFG_GRACE_MS           60000u     /* 60 s */

/* ------------------------------------------------------------------ limits */

#define CFG_MAX_PROCS          256        /* processes tracked in one tree */
#define CFG_MAX_APPS           128        /* rows in the app list          */
#define CFG_LOG_MAX_BYTES      4194304    /* 4 MB, then start over         */

#define CFG_APP_NAME           L"nosleep"
#define CFG_WND_CLASS          L"NoSleepGuiWindow"
#define CFG_MUTEX_NAME         L"Local\\NoSleepGuiSingleInstance"
#define CFG_LOG_NAME           L"nosleep.log"
#define CFG_INI_NAME           L"nosleep.ini"

#endif
