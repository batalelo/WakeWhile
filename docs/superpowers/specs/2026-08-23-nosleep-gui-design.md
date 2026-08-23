# nosleep GUI — Design

**Date:** 2026-08-23
**Status:** Approved

## Problem

`Install-KeepAwake.ps1` gives a terminal command (`nosleep <cmd>`) that holds a
sleep lock for the duration of a wrapped process. It only helps when you launch
the work from a shell. It does nothing for work already running in a GUI app —
an export in a video editor, a build in an IDE, a long upload in a browser.

We want a single tiny executable that closes that gap: pick an app that is
already open, and Windows stays awake for as long as that app is actually doing
something.

## Goals

- One self-contained `.exe`, target **under 15 KB**, no installer, no runtime
  dependency beyond DLLs shipped with Windows.
- Lists the apps the user currently has open, the way Alt+Tab does.
- Holds the sleep lock only while the chosen app is genuinely working, and
  releases it when the app goes quiet.
- Stays out of the way: lives in the tray while monitoring.

## Non-goals

- No replacement for the existing PowerShell tooling; the two coexist.
- No scheduling, profiles, history, or persisted settings.
- No elevation. Only the current user's own processes are monitored.

## The activity rule

This is the heart of the design, and the naive version does not work.

### Measure per core, not per machine

Task Manager divides CPU by the number of logical processors, so one fully busy
core reads 6% on a 16-thread machine and 25% on a 4-thread one. A fixed "5%"
would mean something different on every machine.

We measure **percent of a single core**:

    cpu_permille = (d_kernel_100ns + d_user_100ns) * 1000 / d_wall_100ns

1000 permille = one core fully busy. The number means the same thing everywhere.

### Measure the process tree, not the process

Chrome, VS Code, Discord, and every other Electron or multi-process app put the
real work in child processes. The window the user picks belongs to the parent,
which sits near 0% while a renderer child pegs a core. We sum CPU and I/O over
the selected PID **and all of its descendants**, rebuilt from a
`CreateToolhelp32Snapshot` parent map on every tick.

### CPU alone misses real work

Downloads, uploads, file copies and torrents are real work you do not want
interrupted, and they use almost no CPU. We therefore also sample
`GetProcessIoCounters` and treat sustained I/O as activity.

### The rule

    busy = cpu >= 200 permille (20% of one core)   OR   io >= 1 MB/s

Chosen because there is a wide empty gap between the two populations. Measured
with `tests/probe.c` on a real desktop, whole tree summed:

| app | processes in tree | CPU, % of one core | I/O |
|---|---|---|---|
| Notepad++, idle | 1 | 0 | 0 |
| WinSCP, idle | 1 | 0 – 1.5 | 3 B/s |
| Chrome, idle | 68 | 0 – 4.6 | 0 – 145 KB/s |
| **threshold** | | **20** | **1 MB/s** |
| VS Code running a build | 27 | 10 – 37 | 2.4 MB/s |
| PowerShell burning one core | 4 | 92 – 100 | 0 |

Chrome is the one that settles it: 68 processes summed together, genuinely
idle, and still under 5%. The worry that summing a tree would inflate the
baseline does not survive contact with the data.

5% would sit inside that band and the lock would never release.

### Grace period

CPU is sampled every second. A tick below both thresholds does not release the
lock immediately; the lock is held until **60 consecutive seconds** of quiet.
This covers the natural pauses in real workloads — waiting on I/O, on a network
round trip, or between stages of a build.

The first tick after activation counts as busy, so the lock engages instantly
when the user presses the button.

## Architecture

Three pure modules with no Win32 calls, testable headless, and thin Win32
modules around them.

    main.c     WinMain, timer, wiring
      |
      +-- ui.c        window, owner-drawn list/button/checkbox, DPI, theme
      +-- tray.c      tray icon (drawn at runtime with GDI), context menu
      +-- applist.c   EnumWindows -> visible top-level windows
      +-- power.c     SetThreadExecutionState acquire/release
      |
      +-- monitor.c   toolhelp snapshot -> process tree -> raw samples
             |
             +-- tracker.c    [pure] per-PID table -> aggregate deltas
             +-- activity.c   [pure] deltas + thresholds + grace -> state

| Module | Does | Depends on |
|---|---|---|
| `tracker.c` | Holds the previous sample per PID; turns a fresh snapshot array into aggregate CPU and I/O deltas. Handles processes appearing, exiting, and PID reuse. | nothing |
| `activity.c` | Turns deltas + elapsed time into `BUSY` / `GRACE` / `IDLE`, applying the thresholds and the 60 s grace window. | nothing |
| `monitor.c` | Builds the descendant set of the root PID, opens each, reads `GetProcessTimes` + `GetProcessIoCounters`. Detects root exit. | Win32 |
| `applist.c` | `EnumWindows`, filtered to visible, non-tool, top-level, titled windows. One entry per PID. | Win32 |
| `power.c` | `SetThreadExecutionState`, idempotent acquire/release, optional display flag. | Win32 |
| `tray.c` | `Shell_NotifyIconW`; the icon is a coloured dot drawn into a DIB at runtime, so no resource compiler is needed. | Win32 |
| `ui.c` | The window and its owner-drawn controls. | Win32 |

### Data flow, per tick (1 s)

    monitor_sample(root_pid)
      -> snapshot[] {pid, create_time, cpu_100ns, io_bytes}
        -> tracker_update(snapshot) -> {d_cpu_100ns, d_io_bytes, d_wall_100ns}
          -> activity_update(deltas) -> BUSY | GRACE | IDLE
            -> power_set(state != IDLE)
            -> tray_update(state, cpu_permille)

## Toolchain

TinyCC 0.9.27 x86_64, vendored under `tools/` (1.6 MB, portable, no installer).
Validated: it builds a Win32 GUI executable at 3.5 KB.

Two gaps and their fixes:

1. **Missing imports.** TCC's `kernel32.def` lacks `Process32FirstW`,
   `Process32NextW`, `GetProcessIoCounters`, `QueryFullProcessImageNameW`, and
   there is no `shell32.def` or `advapi32.def` at all. Fixed with small `.def`
   files under `build/` listing only the symbols we use. Verified to load and
   run.
2. **Missing headers.** No `tlhelp32.h`, no `shellapi.h`. Fixed by declaring the
   handful of structs and prototypes we need in `src/wincompat.h`.

TCC also has no resource compiler, so an application manifest cannot be
embedded. That normally means Common Controls fall back to the Windows 95 look.

**We sidestep it by not using Common Controls.** Every control is owner-drawn
with GDI on top of plain `user32` primitives, which gives a flat modern
appearance that does not depend on a manifest, drops the `comctl32` dependency,
and makes dark mode a palette swap.

## User interface

Single fixed-size window, roughly 420 x 520 at 100% DPI, scaled by
`GetDeviceCaps(LOGPIXELSX)`.

    +--------------------------------------+
    |  nosleep                       - x   |
    |                                      |
    |  Keep Windows awake while this app   |
    |  is working                          |
    |                                      |
    |  +--------------------------------+  |
    |  | Blender                        |  |   owner-drawn LISTBOX
    |  | blender.exe - PID 8124         |  |   36 px rows, title plus
    |  |--------------------------------|  |   dim subtitle
    |  | Visual Studio Code             |  |
    |  | Code.exe - PID 4410            |  |
    |  | ...                            |  |
    |  +--------------------------------+  |
    |                                      |
    |  [ ] Keep the screen on too          |
    |                                      |
    |  +--------------------------------+  |
    |  |          DON'T SLEEP           |  |   owner-drawn button
    |  +--------------------------------+  |
    +--------------------------------------+

While monitoring, the window hides to the tray. Restoring it shows the same
window with the list disabled, a status line (`Blender - 340% of one core -
holding`), and the button relabelled `RELEASE`.

Theme follows `AppsUseLightTheme` in the registry; the title bar follows via
`DwmSetWindowAttribute` where available.

### Tray

- Icon: a filled dot drawn at runtime — **green** holding, **amber** in the
  grace window, **grey** released.
- Tooltip: `nosleep - Blender - 340% - holding`.
- Left click restores the window; right click gives Show / Release / Exit.

## Error handling

| Situation | Behaviour |
|---|---|
| Chosen process exits | Release the lock, restore the window, reselect. |
| `OpenProcess` denied for a child | Skip that child, keep monitoring the rest. |
| `SetThreadExecutionState` returns 0 | Show the failure in the status line; keep monitoring. |
| A child exits between ticks | Its delta is dropped for that tick, never counted as negative. |
| PID reused by a new process | Detected by comparing process creation time; treated as a new process. |
| Second instance launched | Restores the first instance's window and exits. |
| Tray area not ready (Explorer restart) | Re-register on the `TaskbarCreated` message. |

The lock is released in `WM_DESTROY`, and Windows drops it automatically if the
process is killed, so a crash cannot leave the machine permanently awake.

## Testing

`tracker.c` and `activity.c` are pure and hold all the logic that is easy to get
wrong. They are tested by `tests/test_activity.c`, a console executable built by
the same compiler, covering:

- CPU normalisation to a single core across differing tick lengths
- the 20% threshold, either side of the boundary
- the I/O path triggering with near-zero CPU
- the 60 s grace window: held at 59 s of quiet, released at 60
- re-acquisition after release
- a child appearing mid-run, and a child exiting mid-run (no negative delta)
- PID reuse detected via creation time
- the first tick counting as busy

Manual verification: run a real workload, confirm `powercfg /requests` lists the
executable under SYSTEM, then let the workload finish and confirm the entry
disappears within 60 seconds.

## Build

`build.cmd` — one file, no arguments, produces `nosleep.exe`. It compiles and
runs the tests first and refuses to produce the executable if any test fails.

## What shipped, and where it differed

| Planned | Actual |
|---|---|
| under 15 KB | **40 KB.** TinyCC does not optimise; `.text` is 31 KB for ~1,900 lines. A `-nostdlib` MinGW build of the same source would land nearer 12 KB. |
| fixed 400x496 window | Height is computed from the monitor work area at startup. A fixed height put the button behind the taskbar on a 1366x768 screen. |
| 60 s grace | unchanged |
| 20% of one core, whole tree, or 1 MB/s | unchanged, and confirmed against measurements |

### Verified

- 191 headless checks over `tracker.c` and `activity.c`.
- `probe.c` against a parent whose child burns a core: the parent reads 0%,
  the tree reads 100%, and when the child exits the tree shrinks with no
  negative delta and the grace clock starts cleanly.
- Tree membership checked against `Win32_Process`: Chrome 68 vs 68,
  VS Code 26 vs 27, Explorer 35 vs 36, single-process apps 1 vs 1.
- Full UI flow driven by synthetic mouse input: pick, engage, hide to tray,
  restore, release, toggle the display flag.
- `SetThreadExecutionState` returned success on every acquire.
  `powercfg /requests` needs an elevated prompt and is left as a manual check.
