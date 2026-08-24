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

   The defaults are set to catch real work rather than to let a noisy editor
   sleep, which is a deliberate choice between two things that cannot both be
   had -- see the note above the wait, below. The consequence is that an IDE
   piping a couple of megabytes a second to itself while nobody touches it
   will hold the lock. Moving the Disk slider right is the answer to that. */

/* CPU, in permille of ONE core: 1000 = one core pegged. Machine-independent
   by construction, unlike the figure a task manager shows, which divides by
   the number of logical processors. 200 is a fifth of one core. */
#define CFG_CPU_DEFAULT        200u
#define CFG_CPU_MIN            50u
#define CFG_CPU_MAX            4000u
#define CFG_CPU_MULTIPLE       3u

/* ReadFile and WriteFile. Named pipes land here too, which is why an idle
   Electron IDE can read megabytes a second: its windows talk to each other
   this way, and Windows counts it as ordinary file I/O. At 1 MB/s such an
   editor stays over the line even when idle. */
#define CFG_DISK_DEFAULT       1048576u   /* 1 MB/s   */
#define CFG_DISK_MIN           65536u     /* 64 KB/s  */
#define CFG_DISK_MAX           268435456u /* 256 MB/s */
#define CFG_DISK_MULTIPLE      2u

/* Sockets. Windows will not give an unprivileged process a per-process
   network byte count, but socket traffic goes through DeviceIoControl and so
   lands in the "other" counter -- measured, a 358 KB/s download showed up as
   331 KB/s of "other" with read and write both flat at zero.
   An idle editor chatters at 1-5.5 KB/s on sync, telemetry and extension
   polling, and a real AI session runs at a median of 1 KB/s with peaks to
   11 KB/s -- so the two overlap almost completely and this bar is set low
   enough to catch the quieter of them. */
#define CFG_NET_DEFAULT        2048u      /* 2 KB/s */
#define CFG_NET_MIN            512u
#define CFG_NET_MAX            4194304u   /* 4 MB/s  */
#define CFG_NET_MULTIPLE       2u

/* Memory, as page faults per second. The working set on its own says nothing
   about activity -- an app holding 2 GB and doing nothing is still doing
   nothing -- but a process that is really working touches new pages
   constantly. */
#define CFG_MEM_DEFAULT        3000u      /* faults/s */
#define CFG_MEM_MIN            500u
#define CFG_MEM_MAX            2000000u
#define CFG_MEM_MULTIPLE       3u

/* --------------------------------------------------------------- releasing

   This, not the thresholds, is what tells work apart from idleness -- and it
   took measuring both to see why.

   Watched over 300 s of real work and 180 s of true idleness, VS Code with an
   AI session open reads HIGHER when idle than when working: 295 permille of a
   core against 205, and 2.28 MB/s of disk against 652 KB/s. While the model
   is generating, the editor itself does almost nothing; while nobody is
   there, its file watcher and extension host hum along steadily. No level
   separates the two, and lowering the bars to catch the work makes idleness
   read as 99 percent busy.

   What does separate them is how long the quiet stretches run:

       working   longest quiet gap   74 s
       idle      longest quiet gap  161 s

   With the tight limits above, an idle editor sits over the line more or
   less continuously, so on that machine the wait rarely gets to run out at
   all. That is the trade the defaults take: never dropping the lock during
   work, at the cost of a noisy editor keeping the machine up. The sliders
   exist because the right side of that trade is not the same for everyone --
   move Disk and CPU right, or Wait left, to get the other one. */
#define CFG_GRACE_DEFAULT     600000u   /* 10 minutes */
#define CFG_GRACE_MIN          30000u
#define CFG_GRACE_MAX        1800000u   /* 30 minutes, so the default is not pinned to the end */

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
