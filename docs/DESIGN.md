# WakeWhile — design

How it works, and why it works that way. This describes what is in the code
today; where a decision looks arbitrary, the measurement behind it is given.

## The shape of it

Three modules with no Win32 in them at all, wrapped in thin Win32 layers. The
hard parts — the ones with subtle failure modes — are the pure ones, so they
can be tested headless.

    app.c        the window, the once-a-second tick, the wiring
      |
      +-- ui.c        owner-drawn list, sliders, fonts, DPI, hit-testing
      +-- theme.c     light and dark palettes
      +-- icon.c      the eye, rasterised at run time
      +-- tray.c      tray icon and menu
      +-- applist.c   EnumWindows -> the open-window list
      +-- power.c     SetThreadExecutionState
      +-- settings.c  the .ini beside the executable
      +-- logfile.c   one line per second
      |
      +-- monitor.c   toolhelp -> process tree -> raw samples      [Win32]
      +-- netstat.c   established connections off this machine     [Win32]
            |
            +-- tracker.c    [pure] per-PID table -> aggregate deltas
            +-- activity.c   [pure] deltas + limits + wait -> state

### Per tick

    monitor_sample(root pid)
      -> snapshot[] {pid, create time, cpu, read/write, other, faults, ws}
        -> tracker_update()  -> aggregate deltas
          -> activity_update() -> BUSY | GRACE | IDLE
            -> power_apply()   (only while watching)
            -> tray_update()
            -> log_tick()

## Measuring the right thing

### The process tree, not the process

Chrome, VS Code, Cursor, Discord and every other Electron app put the real work
in child processes. The window you clicked belongs to a parent that sits near
zero. So every signal is summed over the chosen PID and all of its descendants,
rebuilt from a `CreateToolhelp32Snapshot` parent map on every tick.

Windows reuses PIDs freely, so a process can name a parent it never had. A
genuine descendant cannot predate its root, and that check is what stops an
unrelated process being adopted into the tree.

### CPU as a share of one core

`GetProcessTimes` gives kernel + user time. Divided by elapsed wall time, that
is permille of a single core: 1000 means one core fully busy. Task Manager
instead divides by the logical processor count, which is why one busy core
reads 6% on a 16-thread machine and 25% on a 4-thread one. Measuring per core
makes the limit mean the same thing everywhere.

### Read/write kept apart from everything else

`GetProcessIoCounters` returns three counters. `ReadTransferCount` and
`WriteTransferCount` are file work. `OtherTransferCount` is everything else —
`DeviceIoControl`, and the named pipes Electron apps talk to themselves over.

Lumping them together is what made an idle IDE look permanently busy: two of
VS Code's own processes pass over a megabyte a second between them, and
Windows counts that as ordinary read/write.

### Sockets, indirectly

Windows will not give an unprivileged process a per-process network byte
count; the APIs that do require elevation. But socket traffic goes through
`DeviceIoControl`, so it lands in `OtherTransferCount`. Measured: a 358 KB/s
download appeared as 331 KB/s of "other" with read and write both flat at zero.

That counter is gated on `GetExtendedTcpTable` showing at least one
established connection whose remote address is not `127.x.x.x`, so an app
talking only to itself is never mistaken for one talking to a server.

### Memory as churn, not as level

The working set says nothing about activity — an app holding 2 GB and doing
nothing is still doing nothing. `PageFaultCount` from `GetProcessMemoryInfo`
does: a process that is really working touches new pages constantly. The
working set is carried alongside only so it can be displayed.

## The limits

Each signal is judged against:

    limit = max( where the slider is , this app's recent quiet level x multiple )

The learned part can only ever *raise* a limit, never lower it, so nothing the
program works out on its own can make it more trigger-happy than asked. When
it fires, the row says `auto-raised`.

It is ignored when the long window shows no quiet moment at all. An export
pegged at one core from start to finish has told us nothing about its baseline,
and treating its busy level as a floor is how a render would talk itself into
looking idle halfway through.

Every signal is judged on a five-second mean, so one stray spike is not work
and a burst every few seconds still is.

## The wait — the part that actually decides

The defaults are CPU 20% of a core, disk 1 MB/s, network 2 KB/s, memory 3,000
faults/s. They are tight on purpose: catching real work matters more here than
letting a noisy editor sleep.

That choice was not free, and here is the measurement that forced it. 300
seconds of an active AI coding session in VS Code against 180 seconds of the
same editor genuinely left alone:

| | idle median | working median |
|---|---|---|
| CPU | **295**‰ | 205‰ |
| Disk | **2.28 MB/s** | 652 KB/s |
| Network | **677 B/s** | 467 B/s |
| Memory | **1,234**/s | 699/s |

The idle editor reads higher than the working one on every signal. While the
model generates, the editor waits on a socket and does nothing; while nobody is
there, its file watcher and extension host hum along forever. The distributions
are inverted, not merely overlapping, so **no level separates them**:

| limits | working | idle |
|---|---|---|
| loose | 14% busy, longest quiet gap 74 s | 3% busy, gap 161 s |
| tight | 57% busy, gap 15 s | **99% busy, gap 1 s** |

Loose enough to let an idle machine sleep is also loose enough to drop the lock
mid-work; tight enough to catch the work means an idle machine never sleeps.

What does separate them is the length of the quiet stretches — 74 seconds
working against 161 idle — which is why the wait exists and why it is a slider
rather than a constant. At the shipped limits an idle VS Code sits over a line
more or less continuously, so on that machine the wait rarely runs out at all.
Anyone who wants the other side of the trade moves Disk and CPU right, or Wait
left.

### Why not calibrate from the chosen app?

The obvious idea is to measure the selected app and derive its limits from its
own quiet level. The table above is why it is not implemented: any multiple of
a quiet level lands *above* the working level when the quiet level is the
higher of the two. What survives of the idea is the learned raise described
above.

## Error handling

| Situation | Behaviour |
|---|---|
| Watched process exits | Release, restore the window, go back to the picker |
| `OpenProcess` denied for a child | Skip it, keep watching the rest |
| `SetThreadExecutionState` fails | Say so in the status line, keep watching |
| Child exits between ticks | Its delta is dropped, never counted as negative |
| PID reused | Detected by comparing process creation time |
| Second instance launched | Restores the first instance's window and exits |
| Explorer restarts | Tray icon re-registered on `TaskbarCreated` |
| Toolhelp snapshot fails for a tick | Report no change rather than pretend quiet |

The lock is released in `WM_DESTROY`, and Windows drops it automatically if the
process is killed, so a crash cannot leave the machine permanently awake.

## The interface

Every control is drawn by hand with GDI on top of plain `user32` primitives.
That is not for looks: TinyCC has no resource compiler, so an application
manifest cannot be embedded, and without one the Common Controls fall back to
the Windows 95 appearance. Drawing everything sidesteps the problem entirely,
drops the `comctl32` dependency, and makes following the system light/dark
setting a palette swap.

The window height is computed at startup from the monitor work area rather than
fixed, so the button cannot end up behind the taskbar on a 768 px screen.

The readings start as soon as an app is selected, not when the button is
pressed — they are what you need in order to decide whether to press it. In
that state the sampler feeds the display and nothing else: no power request, no
tray icon, no log.

## The icon

An eye, open while the lock is held and closed once it is let go, so the state
reads at a glance and in greyscale. Colour then says which kind of holding it
is: green working, amber quiet but not yet released.

There is no `.ico` file. The mark is rasterised at run time with integer
arithmetic and 4x4 coverage sampling, geometry in percent of the box, so one
set of numbers serves 16 px in the title bar and whatever a high-DPI screen
asks for. For the file itself, `tools/seticon.c` writes a real icon resource
into the finished executable with `BeginUpdateResource`, drawing through the
same `src/icon.c` so the file and the window cannot drift apart.

It stores six sizes — 16, 20, 24, 32, 40, 48 — and no more. Icon resources hold
raw 32-bit pixels, so a 256 px entry costs 262 KB on its own; storing every
size took the program from 62 KB to 444 KB. What is stored covers 100, 125, 150
and 200 percent scaling exactly, for 26 KB.

## Testing

`tracker.c` and `activity.c` hold everything that is easy to get wrong and have
no Win32 in them, so `tests/test_activity.c` exercises them headless — 92
checks covering CPU normalisation, the process tree, children appearing and
vanishing mid-tick, PID reuse, the smoothing window, the learned raise, the
wait in both directions, and the shipped limits themselves.

Tests that depend on a particular limit pin it explicitly rather than
inheriting the default, so moving a default is a visible decision rather than
something that quietly changes what a test means.

Three things cannot be tested that way and have their own harnesses, none of
them shipped:

| | |
|---|---|
| `tests/probe.c` | watches a real process tick by tick on the console |
| `tests/icon_preview.c` | renders the mark at every size on light and dark |
| `tests/icon_roundtrip.c` | builds the real `HICON` and has Windows draw it back |
