# Drives the real window end to end: pick an app, engage the lock, restore
# from the tray, release. Captures a screenshot at each step.
$ErrorActionPreference = 'Stop'
$root = 'c:\Users\batal\OneDrive\Desktop\nosleep'

Add-Type -AssemblyName System.Drawing
Add-Type -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
[DllImport("user32.dll")] public static extern void mouse_event(uint f, uint x, uint y, uint d, IntPtr e);
[DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
public struct RECT { public int L,T,R,B; }
public struct POINT { public int X,Y; }
'@ -Name U -Namespace Ui

function Get-Win {
    for ($i = 0; $i -lt 40; $i++) {
        $q = Get-Process nosleep -ErrorAction SilentlyContinue
        if ($q) { $q.Refresh(); if ($q.MainWindowHandle -ne 0) { return [IntPtr]$q.MainWindowHandle } }
        Start-Sleep -Milliseconds 250
    }
    return [IntPtr]::Zero
}

function Click-Client {
    param($h, $x, $y)
    $pt = New-Object Ui.U+POINT
    $pt.X = $x; $pt.Y = $y
    [void][Ui.U]::ClientToScreen($h, [ref]$pt)
    [void][Ui.U]::SetCursorPos($pt.X, $pt.Y)
    Start-Sleep -Milliseconds 250
    [Ui.U]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)   # LEFTDOWN
    Start-Sleep -Milliseconds 90
    [Ui.U]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)   # LEFTUP
    Start-Sleep -Milliseconds 450
}

function Shot {
    param($h, $name)
    $r = New-Object Ui.U+RECT
    [void][Ui.U]::GetWindowRect($h, [ref]$r)
    $bmp = New-Object Drawing.Bitmap(($r.R - $r.L), ($r.B - $r.T))
    $g = [Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
    $bmp.Save("$root\build\$name.png", [Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    "  shot: $name.png"
}

Get-Process nosleep -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 400

$app = Start-Process "$root\nosleep.exe" -PassThru
Start-Sleep -Seconds 2
$h = Get-Win
if ($h -eq [IntPtr]::Zero) { throw 'no window' }
[void][Ui.U]::SetForegroundWindow($h)
Start-Sleep -Milliseconds 700

$c = New-Object Ui.U+RECT
[void][Ui.U]::GetClientRect($h, [ref]$c)
"client: $($c.R) x $($c.B)"

"step 1: pick view"
Shot $h 'ui1_pick'

"step 2: click the first row"
Click-Client $h 180 84          # list starts at y=64, row 0 centre ~84
[void][Ui.U]::SetForegroundWindow($h); Start-Sleep -Milliseconds 300
Shot $h 'ui2_selected'

"step 3: press DON'T SLEEP"
Click-Client $h 200 557         # button spans y 534..580
Start-Sleep -Milliseconds 800
$visible = [Ui.U]::IsWindowVisible($h)
$app.Refresh()
"  window visible after pressing: $visible   (expected False)"
"  process alive: $(-not $app.HasExited)   (expected True)"

"step 4: restore from the tray"
[void][Ui.U]::SendMessageW($h, 0x8002, [IntPtr]::Zero, [IntPtr]::Zero)  # WM_APP+2
Start-Sleep -Seconds 2
[void][Ui.U]::SetForegroundWindow($h)
Start-Sleep -Milliseconds 600
"  window visible after restore: $([Ui.U]::IsWindowVisible($h))   (expected True)"
Shot $h 'ui3_watching'

"step 5: press RELEASE"
Click-Client $h 200 557
Start-Sleep -Milliseconds 600
Shot $h 'ui4_released'

"step 6: toggle the screen checkbox"
Click-Client $h 30 513
Shot $h 'ui5_checkbox'

$app.Refresh()
"process still alive at the end: $(-not $app.HasExited)"
Get-Process nosleep -ErrorAction SilentlyContinue | Stop-Process -Force
'done'
