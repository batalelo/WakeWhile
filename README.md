# nosleep

A 60 KB Windows program that keeps the machine awake while a chosen app is
actually working — and lets it sleep again when the app goes quiet.

Open it, pick the app you are waiting on, press **DON'T SLEEP**. It drops to
the tray and watches. When the work finishes, it lets go on its own.

No installer, no dependencies, no background service. One file.

```
nosleep.exe          60 KB, needs nothing but Windows itself
nosleep.log          one line a second, so you can see why it decided what it did
nosleep.ini          where you left the sliders
```

## Why not just prevent sleep?

Because then you have to remember to turn it back on. The point of this is
that the lock releases itself.

The companion `Install-KeepAwake.ps1` in this folder does the same thing for
commands you launch from a terminal (`nosleep npm run build`). This program
covers the other case: work already running inside a GUI app — a video export,
an IDE build, a long upload, an AI agent working through a task.

## When does it think an app is "working"?

Four signals, judged separately, because they behave nothing alike.

| signal | what it is | what it catches |
|---|---|---|
| **CPU** | kernel + user time, as a percentage of **one core** | renders, builds, encodes |
| **Disk** | `ReadFile` + `WriteFile` | copies, exports, big downloads landing on disk |
| **Net** | socket traffic, and only while a connection to another machine is open | AI API sessions, uploads, streaming |
| **RAM** | page faults per second, with the working set shown beside it | anything churning memory |

Each has a slider in the window, with the live reading printed underneath it,
so the line the app has to cross is never abstract. The defaults are set from
measurement (below); move them and they are remembered in `nosleep.ini`.

All three are summed over **the whole process tree** — the app you picked and
everything it has spawned. Chrome, VS Code, Cursor, Antigravity and every
other Electron shell do their real work in child processes; the window you
clicked belongs to a parent that sits near zero.

### Every app has its own idea of "doing nothing"

This is the part that matters, and the obvious design gets it wrong.

Measured on this machine, an **idle** VS Code with nothing being typed into it:

```
cpu   6% - 63% of one core, wandering
disk  2.3 MB/s, second after second, without pause
net   0.7 KB/s
```

Two of its own processes were passing 1.1 MB/s back and forth through a pipe —
which Windows counts as ordinary read/write. Against fixed thresholds that reads
as permanent work, and the machine never sleeps. Notepad, by contrast, sits at
flat zero on all three.

So the bar a signal has to beat is:

    bar = the higher of ( where you put the slider ,
                          what this app has been doing lately x a multiple )

An IDE learns its own noisy baseline and the bars rise to suit it. A quiet app
keeps the slider value. Nothing is hard-coded to any particular editor, so
Cursor, Antigravity, a JetBrains IDE or anything else calibrates itself the
same way. And because the learned part can only ever *raise* the bar, nothing
the program works out on its own can make it more trigger-happy than you asked
for. When that happens the row says `auto-raised`, so the number on screen is
never a lie.

The learned part is **ignored** when the app has been flat for the whole
window. An export pegged at one core from start to finish has never shown a
quiet moment, and letting it treat its own busy level as a baseline is how a
render would talk itself into looking idle halfway through.

Each signal is also judged on a **five-second mean**, so one stray spike is
not work and a burst every few seconds still is.

### Why the network signal exists

An AI API task is mostly *waiting*. Little CPU, no disk, just small bursts of
traffic every few seconds. Windows will not hand an unprivileged process a
per-process network byte count — the APIs that do need an elevated prompt —
but socket traffic goes through `DeviceIoControl`, so it lands in the process's
"other" I/O counter. Measured: a 358 KB/s download appeared as 331 KB/s of
"other", with read and write both flat at zero.

That counter is gated on there being an established connection to another
machine, so an app talking only to itself is never mistaken for one talking to
a server.

The default is **2 KB/s**, and it is low for a reason. Measured over 240
seconds of a real AI session running inside VS Code, against the same editor
sitting idle:

| network, smoothed | idle VS Code | real AI session |
|---|---|---|
| median | 2.8 KB/s | 1.0 KB/s |
| max | 5.5 KB/s | 11.1 KB/s |

The two overlap almost completely — between turns an AI session is *silent*,
so its median is lower than the editor's own background chatter. What
separates them is how long the gaps are, and only a low threshold keeps those
gaps under a minute:

| threshold | longest gap, AI session | longest gap, idle |
|---|---|---|
| **2 KB/s** | **37 s** — holds | **234 s** — sleeps |
| 4 KB/s | 73 s — **sleeps mid-task** | sleeps |
| 16 KB/s | 172 s — **sleeps mid-task** | sleeps |

That leaves 23 seconds of margin. If your work pauses for longer than that,
drag the Net slider left.

### Waiting before letting go

The lock is held until **60 consecutive seconds** of quiet, plus the few
seconds the five-second mean takes to fall. Real workloads pause — on I/O, on
the network, between build stages. One quiet second is not the end of the job.

## The log

`nosleep.log` is written next to the executable, one line per second while
watching. It is there so that "it never lets my machine sleep" has an answer
you can read:

```
2026-08-24 03:08:06  watching: my-project - Visual Studio Code
2026-08-24 03:08:06    pid 7784, plus every process it spawns
2026-08-24 03:08:06    thresholds: cpu 500 permille | disk 4194304 B/s | net 2048 B/s | mem 20000 faults/s
2026-08-24 03:08:08  tick procs=26 cpu=219/500 disk=2316366/4194304 net=122/2048 mem=656/20000 ws=3253MB conn=3 -> grace (-) quiet=0s lock=held
2026-08-24 03:09:12  tick procs=26 cpu=188/500 disk=2049012/4194304 net=679/2048 mem=812/20000 ws=3251MB conn=3 -> IDLE (-) quiet=61s lock=off
```

Each pair is `measured/bar`. A trailing **`r`** means the app's own baseline
raised that bar above the slider. `ws` is the working set across the whole
tree. The word in brackets is which signal decided it — `cpu`, `disk`, `net`,
`mem`, or `-` for none. `lock=held` / `lock=off` is the sleep lock itself.

If the machine is staying awake and you want to know why, find the last line
with something other than `-` in brackets.

The file restarts once it passes 4 MB.

## Using it

- Pick a row, press **DON'T SLEEP**. The window hides; a dot appears by the
  clock: **green** working, **amber** quiet but still holding, **grey**
  released and waiting for work to resume. Windows 11 hides new tray icons by
  default — to pin it: Settings → Personalisation → Taskbar → Other system
  tray icons.
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

All the numbers live in [src/config.h](src/config.h) — thresholds, the grace
window, the sampling rate — with the measurement behind each one written next
to it.

## Watching a process from the console

`build\probe.exe <pid> [seconds]` prints the same numbers the app uses, one
line a second, without touching the GUI. Useful for working out what an app
actually does when you think it is idle.

## Layout

| | |
|---|---|
| `src/tracker.c` | per-PID table → aggregate deltas per channel. No Win32. |
| `src/activity.c` | the rule: bars, baselines, smoothing, grace. No Win32. |
| `src/monitor.c` | process tree, `GetProcessTimes`, `GetProcessIoCounters` |
| `src/netstat.c` | established connections off this machine, per tree |
| `src/applist.c` | the open apps, filtered the way Alt+Tab filters |
| `src/power.c` | `SetThreadExecutionState` |
| `src/logfile.c` | the log |
| `src/ui.c` | drawing primitives and the list control |
| `src/theme.c`, `src/tray.c` | palette, tray icon drawn at run time |
| `src/app.c` | the window and the once-a-second tick |
| `src/settings.c` | the `.ini` beside the executable |
| `tests/test_activity.c` | 85 headless checks over the two pure modules |

The design, and the measurements behind every constant, are in
[docs/superpowers/specs/2026-08-23-nosleep-gui-design.md](docs/superpowers/specs/2026-08-23-nosleep-gui-design.md).
