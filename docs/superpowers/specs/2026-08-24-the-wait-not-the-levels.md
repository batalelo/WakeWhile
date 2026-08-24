# The wait, not the levels — design revision

**Date:** 2026-08-24
**Supersedes the threshold reasoning in:**
[2026-08-24-sliders-and-memory.md](2026-08-24-sliders-and-memory.md)

## What was wrong

The thresholds were chosen to sit *above* an idle VS Code, so that an editor
doing nothing would not hold the lock. That worked, and it broke the other
half: real work never reached them either.

Measured over 300 seconds of an actual working session:

| channel | median | p90 | max | bar | times it fired |
|---|---|---|---|---|---|
| CPU | 205 | 384 | 912 | 500 | 10 of 300 |
| Disk | 652 KB/s | 1.88 MB/s | 4.28 MB/s | 4 MB/s | 0 |
| Net | 467 B/s | 2.5 KB/s | 12.4 KB/s | 2 KB/s | 33 of 300 |
| RAM | 699/s | 3574/s | 8511/s | 20000/s | 0 |

Two of the four channels never fired at all. The program was running on the
network channel alone, and **its longest quiet stretch during real work was 74
seconds** — past the 60-second wait. It would have dropped the lock in the
middle of the work it exists to protect.

## Why lowering the bars cannot fix it

The obvious repair is to lower the thresholds until work reaches them. So the
same 300 seconds of work were compared against 180 seconds of genuine
idleness — the machine left alone, nothing typed, no task running.

| channel | **idle** median | **working** median |
|---|---|---|
| CPU | **295** | 205 |
| Disk | **2.28 MB/s** | 652 KB/s |
| Net | **677 B/s** | 467 B/s |
| RAM | **1234/s** | 699/s |

**Idle reads higher than working on every channel.** It is not an anomaly: while
the model is generating, the editor itself does almost nothing, and while
nobody is there at all, its file watcher and extension host hum along steadily.

So the distributions are not merely overlapping, they are inverted, and no
threshold can separate them:

| bars | working | idle |
|---|---|---|
| current | 14% busy, gap 74 s | 3% busy, gap 161 s |
| lowered | 57% busy, gap 15 s | **99% busy, gap 1 s** |
| lowered further | 86% busy, gap 8 s | **99% busy, gap 1 s** |

Lower the bars enough to catch the work and an idle machine never sleeps —
which is exactly the bug this program started with.

## What does separate them

The length of the quiet stretches:

    working   longest quiet gap    74 s
    idle      longest quiet gap   161 s

That is the only clean separation in the data, and the wait is the parameter
that acts on it. At 60 seconds both sides fail. At 120 the work keeps the lock
with 46 seconds to spare and the idle machine still gets to sleep with 41.

| wait | working | idle |
|---|---|---|
| 60 s | **releases mid-work** | sleeps |
| 90 s | holds | sleeps |
| **120 s** | **holds, 46 s spare** | **sleeps, 41 s spare** |
| 150 s | holds | sleeps, 11 s spare |

## The change

1. The wait goes from 60 seconds to **120**.
2. It becomes the **fifth control** in the window, with the same slider the
   channels have, adjustable from 30 seconds to 10 minutes and remembered in
   `nosleep.ini`. It is not a channel, but it is the control that decides the
   outcome, so it belongs where the user can reach it.
3. The four thresholds stay where they are. The measurements say lowering them
   is actively harmful.

## On calibrating from the chosen app

The obvious idea — measure the selected app and derive its bars — is what
prompted this round, and the data is why it is not implemented. Deriving a bar
from an app's own level fails when its idle level is *higher* than its working
level, which is the case here. Any multiple of the quiet level lands above the
work.

What survives of the idea is already in place: the learned floor still raises a
bar for an app noisier than the defaults expect, and the row says
`auto-raised` when it does. It can only ever raise, never lower, so it cannot
make the program miss work.

## Also in this revision

Both halves of the window get a numbered heading and a line of explanation.
The window was a list and five unlabelled sliders with nothing to say what
either was for.

## Verified

- 89 headless checks, including two new ones for the wait taking effect in
  both directions.
- The Wait slider driven by posted messages: written to `nosleep.ini`, read
  back on restart, restored to 120 s when the file is deleted.
- Window is 436 x 691 on a 1366 x 768 screen, inside the work area.

## Known limits

The working figure comes from one 300-second session and the idle figure from
one 180-second stretch on one machine. A session that pauses for longer than
120 seconds — a very long generation, or stepping away mid-task — will still
release. That is what the Wait slider is for.
