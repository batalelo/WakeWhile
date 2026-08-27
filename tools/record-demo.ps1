# Records the demo clip: WakeWhile holding the lock while a job runs, then
# letting go on its own once the job stops.
#
# The thing worth showing takes ten minutes in normal use, so the Wait is
# turned down to its minimum for the recording and put back afterwards.
# Everything else is real: the job below genuinely loads a core, and every
# number on screen is measured rather than staged.
#
#   .\tools\record-demo.ps1
#
# Leave the machine alone while it runs. Writes docs/images/wakewhile-demo.gif
# and .mp4.

$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)

$FPS      = 5       # frames a second, captured
$WORK_SEC = 16      # how long the job keeps running once recording starts
$TAIL_SEC = 44      # how long to keep watching after it stops
$SPEED    = 5       # playback speedup
$WAIT_MS  = 30000   # the shortest the Wait slider goes

Add-Type -AssemblyName System.Drawing
Add-Type -MemberDefinition @'
public delegate bool EnumProc(System.IntPtr h, System.IntPtr l);
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, System.IntPtr l);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(System.IntPtr h, System.Text.StringBuilder s, int n);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(System.IntPtr h, System.Text.StringBuilder s, int n);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(System.IntPtr h, out uint pid);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern System.IntPtr FindWindowExW(System.IntPtr p, System.IntPtr a, string c, string t);
[DllImport("user32.dll")] public static extern bool GetWindowRect(System.IntPtr h, out RECT r);
[DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(System.IntPtr h, int attr, out RECT r, int size);
[DllImport("user32.dll")] public static extern bool GetClientRect(System.IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool PostMessageW(System.IntPtr h, uint m, System.IntPtr w, System.IntPtr l);
[DllImport("user32.dll")] public static extern bool SetWindowPos(System.IntPtr h, System.IntPtr a, int x, int y, int cx, int cy, uint f);
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(System.IntPtr h);
public struct RECT { public int L,T,R,B; }
'@ -Name Rec -Namespace Demo

function Find-ByClass([string]$cls, [int]$procId) {
    $script:hit = [IntPtr]::Zero
    $cb = [Demo.Rec+EnumProc]{
        param($hh, $ll)
        $sb = New-Object System.Text.StringBuilder 256
        [void][Demo.Rec]::GetClassNameW($hh, $sb, 256)
        $p = 0
        [void][Demo.Rec]::GetWindowThreadProcessId($hh, [ref]$p)
        if ($p -eq $procId -and $sb.ToString() -eq $cls) { $script:hit = $hh }
        return $true
    }
    [void][Demo.Rec]::EnumWindows($cb, [IntPtr]::Zero)
    return $script:hit
}

# The job window is found by its title. Which process ends up owning a console
# window depends on the host: Windows Terminal keeps it for itself, the classic
# host hands it to the shell. The recording launches through conhost so the
# window belongs to the process actually doing the work.
$script:jobPid = 0
function Find-ByTitle([string]$title) {
    $script:hit2 = [IntPtr]::Zero
    $script:jobPid = 0
    $cb = [Demo.Rec+EnumProc]{
        param($hh, $ll)
        $sb = New-Object System.Text.StringBuilder 256
        [void][Demo.Rec]::GetWindowTextW($hh, $sb, 256)
        if ($sb.ToString() -eq $title) {
            $p = 0
            [void][Demo.Rec]::GetWindowThreadProcessId($hh, [ref]$p)
            $script:hit2 = $hh
            $script:jobPid = $p
        }
        return $true
    }
    [void][Demo.Rec]::EnumWindows($cb, [IntPtr]::Zero)
    return $script:hit2
}

$frames    = 'build\frames'
$worker    = $null
$iniBackup = $null

Write-Host ""
Write-Host "  [1/5] setting up" -ForegroundColor Cyan

Get-Process WakeWhile -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 600

if (Test-Path WakeWhile.ini) { $iniBackup = Get-Content WakeWhile.ini -Raw }
if (Test-Path $frames) { Remove-Item $frames -Recurse -Force }
New-Item -ItemType Directory -Force $frames | Out-Null

try {
    # Shortest possible wait, so the release lands inside a recordable clip.
    @"
cpu=200
disk=1048576
net=2048
mem=3000
wait=$WAIT_MS
display=0
"@ | Set-Content WakeWhile.ini -Encoding ASCII

    # The job. It genuinely hashes a megabyte over and over; nothing about the
    # load is staged. It runs long enough to cover the setup and the first part
    # of the recording, then goes quiet.
    $secs = $WORK_SEC + 14
    $job = "`$host.UI.RawUI.WindowTitle='Long job';" +
           "`$sha=[Security.Cryptography.SHA256]::Create();" +
           "`$buf=New-Object byte[] 1048576;" +
           "`$end=(Get-Date).AddSeconds($secs);" +
           "while((Get-Date) -lt `$end){for(`$i=0;`$i -lt 40;`$i++){[void]`$sha.ComputeHash(`$buf)}};" +
           "Start-Sleep -Seconds 600"

    Start-Process conhost -ArgumentList 'powershell', '-NoProfile', '-Command', $job | Out-Null
    Start-Sleep -Seconds 3

    # Put the job at the top of the z-order, so it is the first row in the list
    # and Home selects it. WakeWhile leaves itself out of its own list.
    $wh = Find-ByTitle 'Long job'
    if ($wh -eq [IntPtr]::Zero) { throw 'the job window did not appear' }
    $worker = Get-Process -Id $script:jobPid
    Write-Host "        job: $($worker.ProcessName) pid $($worker.Id)" -ForegroundColor DarkGray

    [void][Demo.Rec]::SetWindowPos($wh, [IntPtr]::Zero, 700, 430, 560, 220, 0x0040)
    [void][Demo.Rec]::SetForegroundWindow($wh)
    Start-Sleep -Seconds 1

    Start-Process "$PWD\WakeWhile.exe"
    Start-Sleep -Seconds 3
    $ww  = (Get-Process WakeWhile).Id
    $win = Find-ByClass 'WakeWhileWindow' $ww
    if ($win -eq [IntPtr]::Zero) { throw 'the WakeWhile window did not appear' }

    [void][Demo.Rec]::SetWindowPos($win, [IntPtr]-1, 60, 30, 0, 0, 0x0041)
    [void][Demo.Rec]::SetForegroundWindow($win)
    Start-Sleep -Milliseconds 900

    Write-Host "  [2/5] picking the job and taking the lock" -ForegroundColor Cyan

    $list = [Demo.Rec]::FindWindowExW($win, [IntPtr]::Zero, 'WakeWhileListView', $null)
    [void][Demo.Rec]::PostMessageW($list, 0x0100, [IntPtr]0x24, [IntPtr]0)   # Home
    Start-Sleep -Seconds 3                                                   # let readings appear

    $c = New-Object Demo.Rec+RECT
    [void][Demo.Rec]::GetClientRect($win, [ref]$c)
    $btnY = $c.B - 60 + 23
    $lp = [IntPtr]($btnY * 65536 + 200)
    [void][Demo.Rec]::PostMessageW($win, 0x0201, [IntPtr]1, $lp)
    Start-Sleep -Milliseconds 250
    [void][Demo.Rec]::PostMessageW($win, 0x0202, [IntPtr]0, $lp)
    Start-Sleep -Milliseconds 700

    # Pressing the button drops the window to the tray; bring it back so the
    # clip can show what it is doing.
    [void][Demo.Rec]::PostMessageW($win, 0x8002, [IntPtr]0, [IntPtr]0)       # MSG_SHOW_ME
    Start-Sleep -Milliseconds 800
    [void][Demo.Rec]::SetWindowPos($win, [IntPtr]-1, 60, 30, 0, 0, 0x0041)
    [void][Demo.Rec]::SetForegroundWindow($win)
    Start-Sleep -Milliseconds 600

    $watched = Get-Content WakeWhile.log -Encoding UTF8 | Select-String 'watching:' | Select-Object -Last 1
    Write-Host "        $watched" -ForegroundColor DarkGray
    if ("$watched" -notmatch 'Long job') { throw "watching the wrong window: $watched" }

    Write-Host "  [3/5] recording" -ForegroundColor Cyan

    # GetWindowRect hands back the *layout* rectangle, which since Windows 10
    # includes an invisible resize border — eight pixels of whatever happens to
    # be sitting behind the window. Capturing that means capturing the desktop,
    # or worse, a stripe of some other window. DWMWA_EXTENDED_FRAME_BOUNDS (9)
    # is the rectangle actually painted on screen.
    $r = New-Object Demo.Rec+RECT
    if ([Demo.Rec]::DwmGetWindowAttribute($win, 9, [ref]$r, 16) -ne 0) {
        [void][Demo.Rec]::GetWindowRect($win, [ref]$r)
        Write-Host "        DWM bounds unavailable, falling back" -ForegroundColor Yellow
    }
    $w = $r.R - $r.L
    $h = $r.B - $r.T
    $bmp = New-Object Drawing.Bitmap($w, $h)
    $g = [Drawing.Graphics]::FromImage($bmp)

    $total = ($WORK_SEC + $TAIL_SEC) * $FPS
    $delay = [int](1000 / $FPS)
    for ($n = 0; $n -lt $total; $n++) {
        $g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
        $bmp.Save(('{0}\f{1:d4}.png' -f $frames, $n), [Drawing.Imaging.ImageFormat]::Png)
        if ($n % ($FPS * 10) -eq 0) {
            Write-Host ("        {0}s of {1}s" -f [int]($n / $FPS), ($WORK_SEC + $TAIL_SEC)) -ForegroundColor DarkGray
        }
        Start-Sleep -Milliseconds $delay
    }
    $g.Dispose()
    $bmp.Dispose()
    Write-Host "        $total frames at ${w}x${h}" -ForegroundColor DarkGray
}
finally {
    Write-Host "  [4/5] putting things back" -ForegroundColor Cyan
    Get-Process WakeWhile -ErrorAction SilentlyContinue | Stop-Process -Force
    if ($worker) { Stop-Process -Id $worker.Id -Force -ErrorAction SilentlyContinue }
    if ($iniBackup) { Set-Content WakeWhile.ini $iniBackup -NoNewline }
    elseif (Test-Path WakeWhile.ini) { Remove-Item WakeWhile.ini }
}

Write-Host "  [5/5] encoding" -ForegroundColor Cyan
$ff = "$PWD\build\ffmpeg\ffmpeg.exe"
New-Item -ItemType Directory -Force docs\images | Out-Null
$outFps = $FPS * $SPEED

# One palette for the whole clip, and no dithering. The interface is flat dark
# panels and text, so 256 colours cover it outright; a dither pattern would add
# noise that is both visible against the flat areas and impossible to compress
# between frames. Encoding at native size rather than scaling down keeps the
# text crisp — the README asks the browser for the width it wants.
& $ff -y -loglevel error -framerate $outFps -i "$frames\f%04d.png" `
      -vf "palettegen=max_colors=256:stats_mode=diff" "$frames\pal.png"
& $ff -y -loglevel error -framerate $outFps -i "$frames\f%04d.png" -i "$frames\pal.png" `
      -lavfi "paletteuse=dither=none:diff_mode=rectangle" -loop 0 `
      docs\images\wakewhile-demo.gif

& $ff -y -loglevel error -framerate $outFps -i "$frames\f%04d.png" `
      -c:v libx264 -pix_fmt yuv420p -crf 20 `
      -vf "scale=trunc(iw/2)*2:trunc(ih/2)*2" docs\images\wakewhile-demo.mp4

Write-Host ""
foreach ($f in 'docs\images\wakewhile-demo.gif', 'docs\images\wakewhile-demo.mp4') {
    if (Test-Path $f) {
        Write-Host ("  {0}  {1:N0} bytes" -f $f, (Get-Item $f).Length) -ForegroundColor Green
    }
}
Write-Host ""
