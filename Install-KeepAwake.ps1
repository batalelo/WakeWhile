#Requires -Version 5.1
<#
.SYNOPSIS
    Installs a global "nosleep" wrapper that keeps Windows awake ONLY while a
    command is running, then releases the lock automatically.

.DESCRIPTION
    Performs three things:
      1. Installs the core engine to %LOCALAPPDATA%\KeepAwake\keepawake.ps1
      2. Registers "nosleep" / "nosleep-pid" functions in your PowerShell
         profile(s), so they work in every terminal and every project.
      3. Adds a VS Code USER-level task, available in every workspace.

    Everything is undone by Uninstall-KeepAwake.ps1.

.PARAMETER SkipVSCode
    Do not touch VS Code's user tasks.json.

.PARAMETER Force
    Do not ask for confirmation when changing the execution policy.

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File .\Install-KeepAwake.ps1
#>
[CmdletBinding()]
param(
    [switch]$SkipVSCode,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

# ------------------------------------------------------------------ constants
$InstallDir  = Join-Path $env:LOCALAPPDATA 'KeepAwake'
$CoreScript  = Join-Path $InstallDir 'keepawake.ps1'
$MarkerStart = '# >>> KeepAwake (nosleep) >>>'
$MarkerEnd   = '# <<< KeepAwake (nosleep) <<<'
$TaskLabel   = 'nosleep: run a command...'
$InputId     = 'nosleepCommand'

# ------------------------------------------------------------------ helpers
function Write-Step { param([string]$Text) Write-Host ""; Write-Host "==> $Text" -ForegroundColor Cyan }
function Write-Ok   { param([string]$Text) Write-Host "    [ok]   $Text" -ForegroundColor Green }
function Write-Note { param([string]$Text) Write-Host "    [--]   $Text" -ForegroundColor DarkGray }
function Write-Warn { param([string]$Text) Write-Host "    [!!]   $Text" -ForegroundColor Yellow }

# Every VS Code derivative (Code, Insiders, VSCodium, Cursor, Windsurf, Trae,
# Antigravity, Kiro, Positron, ...) stores its per-user config in
# %APPDATA%\<AppName>\User and always creates a globalStorage folder in there.
# Detecting that signature covers forks that did not exist when this was written.
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
    $dir = Split-Path -Parent $Path
    if ($dir -and -not (Test-Path -LiteralPath $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }
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

# Strips // and /* */ comments from JSONC while respecting string literals.
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
            if     ($escape)      { $escape = $false }
            elseif ($c -eq '\')   { $escape = $true }
            elseif ($c -eq '"')   { $inString = $false }
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
    # tolerate trailing commas, which VS Code allows but JSON does not
    return ($out -replace ',(\s*[}\]])', '$1')
}

# ------------------------------------------------------------------ 1. engine
Write-Step "Installing the engine to $CoreScript"

$coreScriptBody = @'
#Requires -Version 5.1
<#
    keepawake.ps1 - installed by Install-KeepAwake.ps1
    Keeps Windows awake for the duration of a command, then lets it sleep again.
    Do not edit by hand; re-run the installer instead.
#>
param(
    [switch]$Display,
    [int]$WaitProcessId = 0,
    [string]$CommandLine,
    [Parameter(Position = 0, ValueFromRemainingArguments = $true)]
    [string[]]$Command
)

# Never let a non-zero exit code from the wrapped command become an exception:
# we want to forward it verbatim instead.
$ErrorActionPreference = 'Continue'
$PSNativeCommandUseErrorActionPreference = $false

if ($WaitProcessId -le 0 -and -not $CommandLine -and (-not $Command -or $Command.Count -eq 0)) {
    Write-Host "usage: nosleep [-Display] <command> [args...]" -ForegroundColor Yellow
    Write-Host "       nosleep-pid <processId>" -ForegroundColor Yellow
    exit 2
}

# --- bind to the Win32 power API (only once per session) ---------------------
if (-not ('Win32.KeepAwakePower' -as [type])) {
    $signature = '[DllImport("kernel32.dll", SetLastError = true)] public static extern uint SetThreadExecutionState(uint esFlags);'
    Add-Type -MemberDefinition $signature -Name 'KeepAwakePower' -Namespace 'Win32' | Out-Null
}
$power = [Win32.KeepAwakePower]

$ES_CONTINUOUS       = [uint32]2147483648   # 0x80000000 - make the state stick
$ES_SYSTEM_REQUIRED  = [uint32]1            # 0x00000001 - machine stays awake
$ES_DISPLAY_REQUIRED = [uint32]2            # 0x00000002 - screen stays on

$flags = $ES_CONTINUOUS -bor $ES_SYSTEM_REQUIRED
if ($Display) { $flags = $flags -bor $ES_DISPLAY_REQUIRED }

$previous = $power::SetThreadExecutionState([uint32]$flags)
if ($previous -eq 0) {
    Write-Warning "Could not acquire the sleep lock; the machine may still sleep."
}

$exitCode = 1
try {
    if ($WaitProcessId -gt 0) {
        Write-Host "keepawake: holding the machine awake until PID $WaitProcessId exits..." -ForegroundColor DarkCyan
        Wait-Process -Id $WaitProcessId
        $exitCode = 0
    }
    elseif ($CommandLine) {
        Invoke-Expression $CommandLine
        $exitCode = if ($null -eq $LASTEXITCODE) { 0 } else { $LASTEXITCODE }
    }
    else {
        $exe  = $Command[0]
        $rest = @($Command | Select-Object -Skip 1)
        & $exe @rest
        $exitCode = if ($null -eq $LASTEXITCODE) { 0 } else { $LASTEXITCODE }
    }
}
catch {
    Write-Error $_
    $exitCode = 1
}
finally {
    # Clearing every flag but ES_CONTINUOUS cancels the request. Runs even on
    # failure or Ctrl+C; and if the process dies outright, Windows drops the
    # request automatically anyway.
    [void]$power::SetThreadExecutionState($ES_CONTINUOUS)
}

exit $exitCode
'@

Save-TextFile -Path $CoreScript -Text $coreScriptBody -Bom
Write-Ok "engine written"

# ------------------------------------------------------- 2. execution policy
Write-Step "Checking the PowerShell execution policy"

$effective = Get-ExecutionPolicy
if ($effective -in @('Restricted', 'AllSigned')) {
    Write-Warn "Current policy is '$effective', which blocks profile scripts."
    $answer = 'y'
    if (-not $Force) {
        $answer = Read-Host "    Set CurrentUser policy to RemoteSigned? (y/n)"
    }
    if ($answer -match '^(y|yes)$') {
        Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy RemoteSigned -Force
        Write-Ok "policy set to RemoteSigned for the current user"
    } else {
        Write-Warn "Skipped. The 'nosleep' command will not load until you change it."
    }
} else {
    Write-Ok "policy is '$effective' - fine"
}

# ------------------------------------------------------------- 3. PS profiles
Write-Step "Registering the 'nosleep' command in your PowerShell profile(s)"

$profileBlock = @'
# >>> KeepAwake (nosleep) >>>
# Installed by Install-KeepAwake.ps1 - remove it with Uninstall-KeepAwake.ps1
$KeepAwakeScript = '__CORE_SCRIPT__'
function nosleep {
    # Deliberately a SIMPLE function driven by $args instead of a param()
    # block. PowerShell performs no parameter binding here, so flags that
    # belong to the wrapped command (-Command, -c, -d, -C ...) can never be
    # swallowed by this wrapper. The command is then handed to the engine by
    # explicit name binding, never by splatting.
    $items   = @($args)
    $display = $false
    if ($items.Count -gt 0 -and $items[0] -is [string] -and $items[0] -match '^--?(display|screen)$') {
        $display = $true
        $items   = @($items | Select-Object -Skip 1)
    }
    if ($items.Count -eq 0) {
        Write-Host "usage: nosleep [-Display] <command> [args...]" -ForegroundColor Yellow
        Write-Host "       nosleep-pid <processId>" -ForegroundColor Yellow
        return
    }
    & $KeepAwakeScript -Display:$display -Command ([string[]]$items)
}
function nosleep-pid {
    $items = @($args)
    if ($items.Count -lt 1) {
        Write-Host "usage: nosleep-pid <processId>" -ForegroundColor Yellow
        return
    }
    & $KeepAwakeScript -WaitProcessId ([int]$items[0])
}
# <<< KeepAwake (nosleep) <<<
'@
$profileBlock = $profileBlock.Replace('__CORE_SCRIPT__', $CoreScript)

$docs = [Environment]::GetFolderPath('MyDocuments')
$profilePaths = @(
    (Join-Path $docs 'WindowsPowerShell\Microsoft.PowerShell_profile.ps1')  # Windows PowerShell 5.1
    (Join-Path $docs 'PowerShell\Microsoft.PowerShell_profile.ps1')         # PowerShell 7+
    $PROFILE.CurrentUserCurrentHost
) | Select-Object -Unique

$blockPattern = [regex]::Escape($MarkerStart) + '[\s\S]*?' + [regex]::Escape($MarkerEnd)

foreach ($p in $profilePaths) {
    if (-not $p) { continue }
    $existing = ''
    if (Test-Path -LiteralPath $p) {
        $existing = Get-Content -LiteralPath $p -Raw -ErrorAction SilentlyContinue
        if ($null -eq $existing) { $existing = '' }
    }

    if ($existing -match $blockPattern) {
        Backup-Target -Path $p | Out-Null
        $new = [regex]::Replace($existing, $blockPattern, $profileBlock.Replace('$', '$$'))
        Save-TextFile -Path $p -Text $new -Bom
        Write-Ok "updated $p"
    } else {
        if ($existing) { Backup-Target -Path $p | Out-Null }
        $sep = if ($existing -and -not $existing.EndsWith("`n")) { "`r`n`r`n" } elseif ($existing) { "`r`n" } else { '' }
        Save-TextFile -Path $p -Text ($existing + $sep + $profileBlock + "`r`n") -Bom
        Write-Ok "added to $p"
    }
}

# --------------------------------------------------------------- 4. VS Code
if ($SkipVSCode) {
    Write-Step "VS Code integration"
    Write-Note "skipped (-SkipVSCode)"
} else {
    Write-Step "Adding an editor task (VS Code and every fork of it)"

    $userDirs = Get-EditorUserDirs

    if (-not $userDirs) {
        Write-Warn "No VS Code-family editor found. Launch one once, then re-run this."
    } else {
        foreach ($d in $userDirs) {
            Write-Note ("detected: " + (Split-Path -Leaf (Split-Path -Parent $d)))
        }
    }

    $taskObj = [ordered]@{
        label   = $TaskLabel
        type    = 'shell'
        command = 'powershell'
        args    = @(
            '-NoProfile'
            '-ExecutionPolicy'; 'Bypass'
            '-File'; $CoreScript
            '-CommandLine'; ('${input:' + $InputId + '}')
        )
        presentation = [ordered]@{
            reveal = 'always'
            panel  = 'dedicated'
            clear  = $true
        }
        problemMatcher = @()
    }

    $inputObj = [ordered]@{
        id          = $InputId
        type        = 'promptString'
        description = 'The command to run while the machine is kept awake'
        default     = 'npm run build'
    }

    foreach ($dir in $userDirs) {
        $tasksPath = Join-Path $dir 'tasks.json'
        $parsed    = $null

        if (Test-Path -LiteralPath $tasksPath) {
            Backup-Target -Path $tasksPath | Out-Null
            try {
                $raw    = Get-Content -LiteralPath $tasksPath -Raw
                $parsed = Remove-JsonComments -Text $raw | ConvertFrom-Json
            } catch {
                Write-Warn "Could not parse $tasksPath - leaving it untouched."
                Write-Warn "A backup was made; add the task by hand if you want it."
                continue
            }
        }

        $tasks = @()
        if ($parsed -and $parsed.PSObject.Properties.Name -contains 'tasks' -and $parsed.tasks) {
            $tasks = @($parsed.tasks | Where-Object { $_.label -notlike 'nosleep:*' })
        }
        $tasks += $taskObj

        $inputs = @()
        if ($parsed -and $parsed.PSObject.Properties.Name -contains 'inputs' -and $parsed.inputs) {
            $inputs = @($parsed.inputs | Where-Object { $_.id -ne $InputId })
        }
        $inputs += $inputObj

        $out = [ordered]@{}
        $out['version'] = if ($parsed -and $parsed.version) { $parsed.version } else { '2.0.0' }
        if ($parsed) {
            foreach ($prop in $parsed.PSObject.Properties) {
                if ($prop.Name -notin @('version', 'tasks', 'inputs')) { $out[$prop.Name] = $prop.Value }
            }
        }
        $out['tasks']  = @($tasks)
        $out['inputs'] = @($inputs)

        $json = ($out | ConvertTo-Json -Depth 20)
        Save-TextFile -Path $tasksPath -Text $json
        Write-Ok "task registered in $tasksPath"
    }
}

# ---------------------------------------------------------------- 5. summary
Write-Host ""
Write-Host "-------------------------------------------------------------" -ForegroundColor DarkGray
Write-Host " Done. Open a NEW terminal, then:" -ForegroundColor White
Write-Host ""
Write-Host "   nosleep npm run build          " -NoNewline -ForegroundColor Green
Write-Host "# machine stays awake, screen may sleep"
Write-Host "   nosleep -Display npm run build " -NoNewline -ForegroundColor Green
Write-Host "# keep the screen on too"
Write-Host "   nosleep-pid 12345              " -NoNewline -ForegroundColor Green
Write-Host "# attach to a job already running"
Write-Host ""
Write-Host " In VS Code (any project):  Ctrl+Shift+P -> Tasks: Run Task" -ForegroundColor White
Write-Host " -> '$TaskLabel'" -ForegroundColor White
Write-Host ""
Write-Host " Verify while a job runs (admin prompt):  powercfg /requests" -ForegroundColor White
Write-Host " You should see powershell.exe listed under SYSTEM:" -ForegroundColor DarkGray
Write-Host "-------------------------------------------------------------" -ForegroundColor DarkGray
