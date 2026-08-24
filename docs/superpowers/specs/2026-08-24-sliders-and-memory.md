# Four channels, four sliders — design

**Date:** 2026-08-24
**Follows:** [2026-08-24-activity-rule-revision.md](2026-08-24-activity-rule-revision.md)

## What prompted it

Two things, in this order.

**The measurement came first.** The previous revision guessed that raising the
network threshold to ~15 KB/s would stop an idle IDE holding the lock. Before
changing anything, `tests/probe.c` was run against VS Code for 240 seconds
while a real Claude Code session worked inside it — not a simulation, the
actual API traffic.

| network, smoothed | idle VS Code | real AI session |
|---|---|---|
| median | 2.8 KB/s | 1.0 KB/s |
| p90 | — | 3.8 KB/s |
| max | 5.5 KB/s | 11.1 KB/s |

**The two populations overlap almost completely**, and the AI session's median
is *below* the idle chatter's, because between turns it is silent. Level alone
cannot separate them.

What does separate them is how long the gaps are. For each candidate
threshold, the longest run of consecutive seconds with every channel under bar:

| network threshold | AI session | idle VS Code |
|---|---|---|
| **2 KB/s** | **37 s** — holds | **234 s** — sleeps |
| 4 KB/s | 73 s — sleeps mid-task | sleeps |
| 16 KB/s | 172 s — sleeps mid-task | sleeps |

So the threshold that was already in place was the right one, and the proposed
change would have put the machine to sleep in the middle of exactly the work
it exists to protect. The guess is discarded; 2 KB/s stays.

**The user's request came second**: make all of this adjustable, add memory as
a fourth thing to watch, and show the live reading under each control. Given
two populations that genuinely overlap, handing the trade-off to the person
who knows what they are running is the right answer, not a cleverer constant.

## Memory as a channel

The working set is a level, not an activity: an app holding 2 GB and doing
nothing is still doing nothing. What tracks activity is **page faults per
second** — a process really working touches new pages constantly.

`GetProcessMemoryInfo` gives both. The fault count drives the channel; the
working set is displayed beside it because it is what people expect to see
when a row is labelled RAM. Measured on VS Code idle: 500–5,700 faults/s
against a 3.2 GB working set, so a 20,000 faults/s default sits well clear.

The call wants `PROCESS_VM_READ` on top of the query right. That is granted
for one's own processes without elevation; if it is refused the other three
channels carry on and memory reads zero.

## The bar, restated

The previous revision had `bar = min(absolute, learned)`, where the learned
value could *lower* the bar. With a slider in the picture that is the wrong
way round — the user sets a line and the program should not quietly drop
below it. So:

    bar = max( slider , this app's recent quiet level x multiple )

The learned part can now only ever raise the bar, which is the direction that
matters: an app noisier than the slider anticipated gets more headroom, and
nothing the program learns can make it more trigger-happy than the user asked
for.

The guard that ignores the learned value when the long window shows no quiet
moment (`high >= low * 2`) is still needed, and now for a sharper reason: with
`max`, an export pegged at one core would otherwise have its own busy level
treated as a floor, the bar lifted three times above it, and the machine put
to sleep mid-render.

## Defaults, all from measurement

| channel | unit | default | idle VS Code | real AI session |
|---|---|---|---|---|
| CPU | permille of one core | 500 | ~330 smoothed, 630 peak | p99 485, max 599 |
| Disk | bytes/s | 4 MB/s | 2.3 MB/s | max 2.8 MB/s |
| Net | bytes/s | 2 KB/s | 2.8 KB/s median | 1 KB/s median, 11 KB/s peak |
| RAM | faults/s | 20,000 | 500–5,700 | similar |

Sliders are logarithmic — every one of these spans three or four orders of
magnitude, and a linear control would spend its whole travel in the top
decade. The mapping is done in integers: 256 steps per octave, so it is
reversible without a lookup table.

## The window

Each channel gets a row: label, slider, the threshold in its own units, and
directly beneath it what the watched app is doing right now. When the app's
own baseline has raised a bar, the row says `auto-raised` so the number on
screen is never a lie.

Sliders are drawn and hit-tested inside the main window rather than being
child controls — four of them would otherwise mean four windows, four paint
handlers and four back buffers, for a control that is a rectangle and a
circle.

Settings live in `nosleep.ini` beside the executable: five lines of
`key=value`, written when a grip is released, clamped on the way back in so a
hand-edited file cannot put a threshold where the slider could never express
it. Deleting the file restores the defaults.

## Verified

- 85 headless checks, including the two regressions from the previous
  incident plus one for the learned bar only ever raising.
- `probe.exe` against VS Code: all four channels read real values —
  CPU 250–350 permille, disk 2.5 MB/s, net 0.7–3.6 KB/s, memory 1,200–5,700
  faults/s against a 3.2 GB working set.
- Sliders driven by posted mouse messages: CPU to 3,888 permille, disk down to
  93,696 B/s, both written to `nosleep.ini`, both read back on restart, both
  restored to defaults when the file is deleted.
- The window is 436 x 693 on a 1366 x 768 screen — inside the work area.

## Known limits

- The screen was locked for the final round of work, so the redesigned window
  was verified by geometry, by the log and by driving it with messages, not by
  eye. It needs one look on an unlocked screen.
- 2 KB/s of network leaves 23 seconds of margin between the AI session's
  longest gap (37 s) and the grace window (60 s). A session that pauses longer
  than that — a very slow turn, or a task genuinely finished — will release.
  That is the intended behaviour, but it is a narrow gap and the slider is
  there for people who want it narrower still.
