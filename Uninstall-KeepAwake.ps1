#Requires -Version 5.1
<#
.SYNOPSIS
    Removes everything Install-KeepAwake.ps1 added.

.DESCRIPTION
    Reverses the install in four steps:
      1. Releases any sleep lock still held by this session.
      2. Strips the "nosleep" block out of your PowerShell profile(s).
      3. Removes the "nosleep:" tasks from VS Code's user tasks.json.
      4. Deletes %LOCALAPPDATA%\KeepAwake.

    A timestamped backup is written next to every file it edits.
    The execution policy is deliberately NOT reverted - other tools may rely on it.

.PARAMETER KeepBackups
    Keep the *.keepawake-backup-* files left behind by the installer.
    Without this switch they are deleted at the end.

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File .\Uninstall-KeepAwake.ps1
#>
[CmdletBinding()]
param(
    [switch]$KeepBackups
)

$ErrorActionPreference = 'Stop'

# ------------------------------------------------------------------ constants
$InstallDir  = Join-Path $env:LOCALAPPDATA 'KeepAwake'
$MarkerStart = '# >>> KeepAwake (nosleep) >>>'
$MarkerEnd   = '# <<< KeepAwake (nosleep) <<<'
$InputId     = 'nosleepCommand'

# ------------------------------------------------------------------ helpers
function Write-Step { param([string]$Text) Write-Host ""; Write-Host "==> $Text" -ForegroundColor Cyan }
function Write-Ok   { param([string]$Text) Write-Host "    [ok]   $Text" -ForegroundColor Green }
function Write-Note { param([string]$Text) Write-Host "    [--]   $Text" -ForegroundColor DarkGray }
function Write-Warn { param([string]$Text) Write-Host "    [!!]   $Text" -ForegroundColor Yellow }

function Get-EditorUserDirs {
    $result = @()
    if (-not $env:APPDATA -or -not (Test-Path -LiteralPath $env:APPDATA)) { return $result }
    foreach ($dir in (Get-ChildItem -LiteralPath $env:APPDATA -Directory -ErrorAction SilentlyContinue)) {
        $userDir = Join-Path $dir.FullName 'User'
        if (Test-Path -LiteralPath (Join-Path $userDir 'globalStorage')) { $result += $userDir }
    }
    return @($result | Select-Object -Unique)
}

function Save-TextFile {
    param([string]$Path, [string]$Text, [switch]$Bom)
    $enc = New-Object System.Text.UTF8Encoding($Bom.IsPresent)
    [System.IO.File]::WriteAllText($Path, $Text, $enc)
}

function Backup-Target {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $bak   = "$Path.keepawake-backup-$stamp"
    Copy-Item -LiteralPath $Path -Destination $bak -Force
    return $bak
}

function Remove-JsonComments {
    param([string]$Text)
    $sb       = New-Object System.Text.StringBuilder
    $inString = $false
    $escape   = $false
    $i        = 0
    while ($i -lt $Text.Length) {
        $c = $Text[$i]
        if ($inString) {
            [void]$sb.Append($c)
            if     ($escape)    { $escape = $false }
            elseif ($c -eq '\') { $escape = $true }
            elseif ($c -eq '"') { $inString = $false }
            $i++; continue
        }
        if ($c -eq '"') { $inString = $true; [void]$sb.Append($c); $i++; continue }
        if ($c -eq '/' -and ($i + 1) -lt $Text.Length) {
            $n = $Text[$i + 1]
            if ($n -eq '/') {
                while ($i -lt $Text.Length -and $Text[$i] -ne "`n") { $i++ }
                continue
            }
            if ($n -eq '*') {
                $i += 2
                while (($i + 1) -lt $Text.Length -and -not ($Text[$i] -eq '*' -and $Text[$i + 1] -eq '/')) { $i++ }
                $i += 2
                continue
            }
        }
        [void]$sb.Append($c)
        $i++
    }
    $out = $sb.ToString()
    return ($out -replace ',(\s*[}\]])', '$1')
}

$backupsMade = New-Object System.Collections.Generic.List[string]

# ------------------------------------------------------- 1. release the lock
Write-Step "Releasing any sleep lock held right now"
try {
    if (-not ('Win32.KeepAwakeUninstall' -as [type])) {
        $signature = '[DllImport("kernel32.dll", SetLastError = true)] public static extern uint SetThreadExecutionState(uint esFlags);'
        Add-Type -MemberDefinition $signature -Name 'KeepAwakeUninstall' -Namespace 'Win32' | Out-Null
    }
    [void][Win32.KeepAwakeUninstall]::SetThreadExecutionState([uint32]2147483648)  # ES_CONTINUOUS alone = cancel
    Write-Ok "lock released for this session"
} catch {
    Write-Note "nothing to release"
}

# ------------------------------------------------------------- 2. PS profiles
Write-Step "Cleaning the 'nosleep' block out of your PowerShell profile(s)"

$docs = [Environment]::GetFolderPath('MyDocuments')
$profilePaths = @(
    (Join-Path $docs 'WindowsPowerShell\Microsoft.PowerShell_profile.ps1')
    (Join-Path $docs 'PowerShell\Microsoft.PowerShell_profile.ps1')
    $PROFILE.CurrentUserCurrentHost
) | Select-Object -Unique

$blockPattern = [regex]::Escape($MarkerStart) + '[\s\S]*?' + [regex]::Escape($MarkerEnd) + '\r?\n?'
$touchedProfile = $false

foreach ($p in $profilePaths) {
    if (-not $p -or -not (Test-Path -LiteralPath $p)) { continue }
    $text = Get-Content -LiteralPath $p -Raw -ErrorAction SilentlyContinue
    if (-not $text -or $text -notmatch $blockPattern) { continue }

    $bak = Backup-Target -Path $p
    if ($bak) { $backupsMade.Add($bak) }

    $new = [regex]::Replace($text, $blockPattern, '')
    $new = $new -replace '(\r?\n){3,}', "`r`n`r`n"   # tidy up leftover blank lines
    Save-TextFile -Path $p -Text $new.TrimEnd() -Bom
    Write-Ok "cleaned $p"
    $touchedProfile = $true
}
if (-not $touchedProfile) { Write-Note "no profile contained the block" }

# ---------------------------------------------------------------- 3. VS Code
Write-Step "Removing the 'nosleep' tasks from every VS Code-family editor"

$userDirs = Get-EditorUserDirs

$touchedTasks = $false

foreach ($dir in $userDirs) {
    $tasksPath = Join-Path $dir 'tasks.json'
    if (-not (Test-Path -LiteralPath $tasksPath)) { continue }

    try {
        $raw    = Get-Content -LiteralPath $tasksPath -Raw
        $parsed = Remove-JsonComments -Text $raw | ConvertFrom-Json
    } catch {
        Write-Warn "Could not parse $tasksPath - left untouched. Delete the 'nosleep:' task by hand."
        continue
    }

    $hasTask  = $parsed.PSObject.Properties.Name -contains 'tasks'  -and @($parsed.tasks  | Where-Object { $_.label -like 'nosleep:*' }).Count -gt 0
    $hasInput = $parsed.PSObject.Properties.Name -contains 'inputs' -and @($parsed.inputs | Where-Object { $_.id -eq $InputId }).Count -gt 0
    if (-not $hasTask -and -not $hasInput) { continue }

    $bak = Backup-Target -Path $tasksPath
    if ($bak) { $backupsMade.Add($bak) }

    $tasks = @()
    if ($parsed.PSObject.Properties.Name -contains 'tasks' -and $parsed.tasks) {
        $tasks = @($parsed.tasks | Where-Object { $_.label -notlike 'nosleep:*' })
    }
    $inputs = @()
    if ($parsed.PSObject.Properties.Name -contains 'inputs' -and $parsed.inputs) {
        $inputs = @($parsed.inputs | Where-Object { $_.id -ne $InputId })
    }

    $out = [ordered]@{}
    $out['version'] = if ($parsed.version) { $parsed.version } else { '2.0.0' }
    foreach ($prop in $parsed.PSObject.Properties) {
        if ($prop.Name -notin @('version', 'tasks', 'inputs')) { $out[$prop.Name] = $prop.Value }
    }
    $out['tasks'] = @($tasks)
    if ($inputs.Count -gt 0) { $out['inputs'] = @($inputs) }

    Save-TextFile -Path $tasksPath -Text ($out | ConvertTo-Json -Depth 20)
    Write-Ok "cleaned $tasksPath"
    $touchedTasks = $true
}
if (-not $touchedTasks) { Write-Note "no VS Code task to remove" }

# ------------------------------------------------------------- 4. engine dir
Write-Step "Deleting the engine folder"
if (Test-Path -LiteralPath $InstallDir) {
    Remove-Item -LiteralPath $InstallDir -Recurse -Force
    Write-Ok "removed $InstallDir"
} else {
    Write-Note "$InstallDir was not there"
}

# --------------------------------------------------------------- 5. backups
Write-Step "Backups"
if ($KeepBackups) {
    if ($backupsMade.Count -eq 0) { Write-Note "none were created" }
    foreach ($b in $backupsMade) { Write-Ok "kept $b" }
} else {
    $all = @()
    foreach ($p in $profilePaths) {
        if ($p) {
            $d = Split-Path -Parent $p
            if (Test-Path -LiteralPath $d) {
                $all += Get-ChildItem -LiteralPath $d -Filter '*.keepawake-backup-*' -File -ErrorAction SilentlyContinue
            }
        }
    }
    foreach ($d in $userDirs) {
        $all += Get-ChildItem -LiteralPath $d -Filter '*.keepawake-backup-*' -File -ErrorAction SilentlyContinue
    }
    $all = $all | Select-Object -Unique
    if (-not $all) {
        Write-Note "none to clean up"
    } else {
        foreach ($f in $all) {
            Remove-Item -LiteralPath $f.FullName -Force
            Write-Ok "deleted $($f.Name)"
        }
    }
}

# ---------------------------------------------------------------- 6. summary
Write-Host ""
Write-Host "-------------------------------------------------------------" -ForegroundColor DarkGray
Write-Host " Uninstalled. Close every open terminal so the old 'nosleep'" -ForegroundColor White
Write-Host " function is dropped from memory." -ForegroundColor White
Write-Host ""
Write-Host " The execution policy was left as-is on purpose. To revert it:" -ForegroundColor DarkGray
Write-Host "   Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy Undefined" -ForegroundColor DarkGray
Write-Host "-------------------------------------------------------------" -ForegroundColor DarkGray
