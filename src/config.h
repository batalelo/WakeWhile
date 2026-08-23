#ifndef NOSLEEP_CONFIG_H
#define NOSLEEP_CONFIG_H

/* Tunables live here and nowhere else.
   Rationale for the numbers is in
   docs/superpowers/specs/2026-08-23-nosleep-gui-design.md. */

/* CPU is measured in permille of ONE core, so the threshold means the same
   thing on a 4-thread laptop and a 32-thread workstation. 1000 = one core
   pegged. 200 sits in the empty gap between idle-app noise (0-50) and real
   work (500+); 50 would fire on Discord's animations alone. */
#define CFG_CPU_BUSY_PERMILLE  200u

/* Downloads, uploads and file copies are real work at almost no CPU cost,
   so sustained I/O counts as busy on its own. */
#define CFG_IO_BUSY_BPS        1048576u   /* 1 MB/s */

/* A single quiet tick must not drop the lock: real workloads pause on I/O,
   on the network, and between build stages. */
#define CFG_GRACE_MS           60000u     /* 60 s */

#define CFG_POLL_MS            1000u

#define CFG_MAX_PROCS          256        /* processes tracked in one tree */
#define CFG_MAX_APPS           128        /* rows in the app list          */

#define CFG_APP_NAME           L"nosleep"
#define CFG_WND_CLASS          L"NoSleepGuiWindow"
#define CFG_MUTEX_NAME         L"Local\\NoSleepGuiSingleInstance"

#endif
