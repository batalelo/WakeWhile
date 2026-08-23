# nosleep

A 40 KB Windows program that keeps the machine awake while a chosen app is
actually working — and lets it sleep again when the app goes quiet.

Open it, pick the app you are waiting on, press **DON'T SLEEP**. It drops to
the tray and watches. When the work finishes, it lets go on its own.

No installer, no dependencies, no background service. One file.

```
nosleep.exe          40 KB, needs nothing but Windows itself
```

## Why not just prevent sleep?

Because then you have to remember to turn it back on. The point of this is
that the lock releases itself.

The companion `Install-KeepAwake.ps1` in this folder does the same thing for
commands you launch from a terminal (`nosleep npm run build`). This program
covers the other case: work already running inside a GUI app — a video export,
an IDE build, a long upload.

## When does it think an app is "working"?

Three decisions matter, and the obvious version of each is wrong.

**It measures percent of one core, not the number Task Manager shows.**
Task Manager divides by your core count, so one fully busy core reads 6% on a
16-thread machine and 25% on a 4-thread one. Here, 100% always means one core.

**It measures the whole process tree.** Chrome, VS Code, Discord and every
Electron app do their work in child processes; the window you picked belongs
to the parent, which sits near zero. Everything the app has spawned counts.

**It counts I/O as work.** Downloads, uploads and file copies use almost no
CPU and are exactly the thing you do not want interrupted.

    working  =  CPU >= 20% of one core   OR   I/O >= 1 MB/s

Measured on a real desktop to check the threshold sits in empty space:

| app | processes | CPU, idle |
|---|---|---|
| Chrome | 68 | 0 – 4.6% |
| Notepad++ | 1 | 0% |
| WinSCP | 1 | 0% |
| **threshold** | | **20%** |
| VS Code running a build | 27 | 10 – 37%, plus 2.4 MB/s |

A 5% threshold would sit inside the idle band and never release.

**It waits 60 seconds before letting go.** Real workloads pause — on I/O, on
the network, between build stages. One quiet second is not the end of the job.

## Using it

- Pick a row, press **DON'T SLEEP**. The window hides; a dot appears by the
  clock: **green** working, **amber** quiet but still holding, **grey**
  released and waiting for work to resume.
- Left-click the tray icon to bring the window back, right-click for
  Show / Release / Exit.
- **Keep the screen on too** is off by default — the machine stays awake but
  the display is free to turn off and save power.
- Closing the window while it is watching just hides it. Release or Exit from
  the tray to stop.
- If the app you picked exits, the lock is released and the window comes back.

To confirm Windows really is holding the lock, from an **admin** prompt:

```
powercfg /requests
```

`nosleep.exe` should be listed under SYSTEM while the dot is green or amber,
and gone once it turns grey.

## Building it

```
powershell -ExecutionPolicy Bypass -File tools\get-tcc.ps1   (once)
build.cmd
```

`get-tcc.ps1` downloads TinyCC — 480 KB, unzips to 1.6 MB, no installer, and
deleting `tools\tcc` undoes it completely. `build.cmd` runs the tests first
and refuses to produce the executable if any fail.

There is no manifest and no `comctl32`: TinyCC has no resource compiler, so
every control is drawn with GDI instead. That is also why dark mode and the
scrollbar look right.

## Layout

| | |
|---|---|
| `src/tracker.c` | per-PID table → aggregate deltas. No Win32. |
| `src/activity.c` | deltas + thresholds + grace → busy/grace/idle. No Win32. |
| `src/monitor.c` | process tree, `GetProcessTimes`, `GetProcessIoCounters` |
| `src/applist.c` | the open apps, filtered the way Alt+Tab filters |
| `src/power.c` | `SetThreadExecutionState` |
| `src/ui.c` | drawing primitives and the list control |
| `src/theme.c`, `src/tray.c` | palette, tray icon drawn at run time |
| `src/app.c` | the window and the once-a-second tick |
| `tests/test_activity.c` | 191 headless checks over the two pure modules |
| `tests/probe.c` | `probe <pid> [seconds]` — watch a real process from a console |

The design and the reasoning behind the numbers are in
[docs/superpowers/specs/2026-08-23-nosleep-gui-design.md](docs/superpowers/specs/2026-08-23-nosleep-gui-design.md).
