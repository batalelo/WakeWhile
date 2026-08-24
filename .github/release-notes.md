WakeWhile holds Windows awake while an app you choose is genuinely busy, and
releases the lock by itself once it goes quiet. No toggle to forget about.

- Pick any open window; watches the whole process tree
- Four signals: CPU (as a share of one core), disk, network, page faults
- Adjustable limits with live readings, shown as soon as you select an app
- Adjustable quiet period before the lock is released
- Tray eye icon: open while the lock is held, closed once it is not
- Per-second plain-text log, so "why won't my PC sleep" has an answer
- Optional keep-screen-on, and follows the Windows light/dark theme

**Requires Windows 10 or 11, 64-bit.** No installer, no dependencies, no
administrator rights. Download the `.exe` below and run it.

Built by GitHub Actions from this tag on a clean runner, so the binary matches
the source in this repository.

```
file    : __ASSET__
size    : __SIZE__ KB
sha-256 : __HASH__
```

Check it before running:

```
certutil -hashfile __ASSET__ SHA256
```
