#ifndef NOSLEEP_CONFIG_H
#define NOSLEEP_CONFIG_H

/* Tunables live here and nowhere else.
   Rationale, and the measurements behind the numbers, are in
   docs/superpowers/specs/2026-08-23-nosleep-gui-design.md. */

/* ---------------------------------------------------------------- sampling */

#define CFG_POLL_MS            1000u

/* A burst of work and a one-second blip look the same at a single sample, so
   every channel is judged on a short rolling mean instead. Five seconds is
   long enough to ignore a stray spike and short enough to notice a stream of
   API responses arriving every few seconds. */
#define CFG_SMOOTH_SAMPLES     5

/* How far back we look for an app's own quiet level. */
#define CFG_FLOOR_SAMPLES      180        /* 3 minutes */

/* Before this many samples exist the window is too short to say anything
   about an app's baseline, so only the absolute bars apply. Release cannot
   happen before the grace window elapses anyway. */
#define CFG_FLOOR_MIN_SAMPLES  30

/* ------------------------------------------------------------- the channels

   Every app has its own idea of "doing nothing". An IDE -- VS Code, Cursor,
   Antigravity, any Electron shell -- pipes megabytes a second between its own
   processes and burns a quarter of a core while sitting untouched, where
   Notepad does neither. One fixed threshold cannot serve both, so each
   channel has two bars and takes the lower one that applies:

     absolute   work that is unambiguous whoever is doing it
     relative   a departure from what THIS app has been doing lately

   The relative bar is ignored when the app has been flat for the whole
   window -- otherwise a video export pegged at one core would treat its own
   busy level as its baseline and conclude it had nothing to do. */

/* CPU, in permille of ONE core: 1000 = one core pegged. Machine-independent
   by construction, unlike the figure a task manager shows. */
#define CFG_CPU_ABSOLUTE       800u       /* 80% of a core: unambiguous work */
#define CFG_CPU_MULTIPLE       3u
#define CFG_CPU_MARGIN         300u       /* 30% of a core above the floor   */

/* ReadFile and WriteFile. Named pipes land here too, which is why an idle
   Electron IDE reads 2.3 MB/s: its windows talk to each other this way. */
#define CFG_DISK_ABSOLUTE      8388608u   /* 8 MB/s */
#define CFG_DISK_MULTIPLE      2u
#define CFG_DISK_MARGIN        1048576u   /* 1 MB/s */

/* Sockets. Windows will not give an unprivileged process a per-process
   network byte count, but socket traffic goes through DeviceIoControl and so
   lands in the "other" counter -- measured: a 358 KB/s download showed up as
   331 KB/s of "other", with read and write both flat at zero. That makes
   this the one clean signal for work that is mostly waiting on a server,
   which is exactly what an AI API task looks like. */
#define CFG_NET_ABSOLUTE       2048u      /* 2 KB/s: covers a steady stream that
                                              never lets the window see quiet */
#define CFG_NET_MULTIPLE       1u
#define CFG_NET_MARGIN         1024u      /* 1 KB/s above this app.s own floor */

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

#endif
