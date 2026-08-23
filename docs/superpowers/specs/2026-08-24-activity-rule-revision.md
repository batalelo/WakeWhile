# Revising the activity rule — design

**Date:** 2026-08-24
**Supersedes:** the "activity rule" section of
[2026-08-23-nosleep-gui-design.md](2026-08-23-nosleep-gui-design.md)

## What went wrong

The shipped rule was:

    busy = cpu >= 200 permille (20% of one core)  OR  io >= 1 MB/s

with `io` being every I/O counter added together. On VS Code it held the sleep
lock permanently, and the machine never slept.

Ninety seconds of measurement with the machine untouched, whole process tree
summed:

| channel | idle VS Code (30 processes) |
|---|---|
| cpu | 6% – 63% of one core, wandering, mean ~24% |
| read + write | **2.3 MB/s, every second, without pause** |
| other | 0.7 KB/s |

Both thresholds sat inside VS Code's idle noise. Two of its own processes were
passing 1.1 MB/s back and forth — a named pipe between the renderer and the
pty host, which Windows counts as ordinary `ReadFile` and `WriteFile`.

The same rule works perfectly on Notepad, WinSCP (flat zero) and Chrome (under
5% of a core across 68 processes). There is no single pair of numbers that
serves both an Electron IDE and a text editor.

## What "network activity" costs on Windows

The user's real goal is an AI API session inside an IDE: little CPU, no disk,
small bursts of traffic every few seconds while the model is working.

Windows will not give an unprivileged process a per-process **network byte
count**. `GetPerTcpConnectionEStats` needs the ESTATS collection enabled,
which needs an elevated prompt; the kernel network ETW provider needs one too;
SRUM needs one and only updates hourly. Elevation is not acceptable for this
program.

What is available without it:

- **`GetProcessIoCounters`** — the `OtherTransferCount` field. Socket
  operations go through `DeviceIoControl`, so they land here. Measured: a
  358 KB/s download appeared as 331 KB/s of "other", with read and write both
  flat at zero. A 3.7 KB/s trickle appeared as 1.7 KB/s — the counter tracks
  transfer bytes per operation, not payload, so it under-reports at small
  sizes but tracks the shape faithfully.
- **`GetExtendedTcpTable`** — which processes hold which connections. No byte
  counts, but enough to know whether an app is talking to anything at all.

That is sufficient. The "other" counter is the signal; the connection table
gates it, so an app talking only to itself is never mistaken for one talking
to a server.

## The revised rule

Three channels, measured over the whole process tree, each judged
independently:

| channel | source |
|---|---|
| `cpu` | kernel + user time, permille of one core |
| `disk` | `ReadTransferCount + WriteTransferCount` |
| `net` | `OtherTransferCount`, only while an established off-machine connection exists |

Each is smoothed over **five seconds** before being judged, so a single spike
is not work and a burst every few seconds still is.

Each is compared against **the lower of two bars**:

    bar = min( absolute , low * multiple + margin )

where `low` is the smallest value seen in the last three minutes.

| channel | absolute | multiple | margin | idle VS Code bar | idle Notepad bar |
|---|---|---|---|---|---|
| cpu | 800 permille | 3 | 300 permille | ~530 | 300 |
| disk | 8 MB/s | 2 | 1 MB/s | ~5.5 MB/s | 1 MB/s |
| net | 2 KB/s | 1 | 1 KB/s | ~1.7 KB/s | 1 KB/s |

A noisy IDE raises its own bars. A quiet app keeps the sensitive ones. Nothing
is hard-coded to any particular editor, so Cursor, Antigravity, a JetBrains
IDE or anything else calibrates itself the same way.

### Why the learned bar is sometimes refused

An export pegged at one core from the first second to the last has never shown
a quiet moment. Letting it treat its own busy level as a baseline is how a
render talks itself into looking idle halfway through, and the machine sleeps
mid-job.

So the learned bar only applies when the window shows the app has genuinely
been quieter at some point:

    high >= low * 2      and at least 30 samples collected

Otherwise the absolute bar stands. A flat-out job therefore stays busy
indefinitely; a wandering IDE gets its baseline learned.

### Why the network absolute is only 2 KB/s

If an app streams steadily for longer than the three-minute window, the
learned bar is refused (nothing quiet in the window) and the absolute one
applies. A 2 KB/s bar covers a steady API stream; VS Code idles at 0.7 KB/s
and Chrome at under 0.6 KB/s, so there is room beneath it.

## Verification

Measured, not assumed. `tests/probe.c` prints every number the rule looks at,
one line a second, for any pid.

| scenario | result |
|---|---|
| VS Code, untouched, 120 s | `grace` from second 2, **`IDLE` at 61.8 s** — lock released. CPU bar learned down to 531, net bar to 2340. |
| VS Code + simulated AI session (8 s of ~3.7 KB/s traffic, 10 s gap, repeating) | every burst caught as `BUSY (net)`; the lock was **never released** across 100 s |
| Parent idle, child burning one core | parent reads 0%, tree reads 100% |
| Chrome, idle, 68 processes | 0 – 4.6% of a core; two genuine network bursts correctly flagged |
| Notepad, WinSCP | flat zero, releases on schedule |
| Process tree vs `Win32_Process` | Chrome 68 vs 68, VS Code 26 vs 27, Explorer 35 vs 36 |
| Network attribution | 358 KB/s download → 331 KB/s of "other", 0 read, 0 write |

81 headless checks cover the pure modules, including two regressions written
directly from this incident: *an idle IDE is allowed to sleep*, and *network
bursts inside a noisy IDE hold the lock*.

## The log

`nosleep.log`, written beside the executable, one line per tick:

    tick procs=26 cpu=203/800 disk=2442811/8388608 net=138/2048 conn=3 -> grace (-) quiet=0s lock=held

Each pair is `measured/bar`; a trailing `r` marks a bar the app's own baseline
lowered; the word in brackets is which channel decided it. Written as UTF-8
with a byte-order mark, because app titles are not all Latin. Restarts past
4 MB. Opened shared, so it can be tailed while running.

## Consequences

- The effective release time is 60 s of quiet **plus** the few seconds the
  five-second mean takes to fall — around 65 s in practice, not 60 exactly.
- `OtherTransferCount` under-reports small transfers. The bars are set from
  measured values rather than from payload sizes, so this is accounted for,
  but it is why the network numbers look smaller than a bandwidth monitor's.
- An app with steady background network chatter above its own learned bar will
  hold the lock. For a browser that means genuine background sync counts as
  work. This is the intended direction to fail in.
