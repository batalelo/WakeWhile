# Fetches the compiler. Run once; build.cmd does the rest.
#
# TinyCC is a 480 KB download that unzips to 1.6 MB, needs no installer, and
# leaves nothing behind if you delete tools\tcc. It is the whole toolchain.
#
# Savannah goes down often enough that a single attempt is not good enough for
# CI or for anyone cloning this on a bad day, so every known mirror is tried in
# turn, twice each, and the archive is checked against a known hash before it
# is trusted.

$ErrorActionPreference = 'Stop'

$here = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
$dest = Join-Path $here 'tcc'
$zip  = Join-Path $here 'tcc.zip'

$expectedSize = 489586
$expectedHash = '34a721949a2583fdff725312da092fa0f5f1f284b702e6f811c6954714faabb2'

# In practice download-mirror answers when the other two do not.
$mirrors = @(
    'https://download-mirror.savannah.gnu.org/releases/tinycc/tcc-0.9.27-win64-bin.zip',
    'https://download.savannah.gnu.org/releases/tinycc/tcc-0.9.27-win64-bin.zip',
    'https://download.savannah.nongnu.org/releases/tinycc/tcc-0.9.27-win64-bin.zip'
)

if (Test-Path (Join-Path $dest 'tcc.exe')) {
    Write-Host "  already present: $dest" -ForegroundColor DarkGray
    & (Join-Path $dest 'tcc.exe') -v
    exit 0
}

[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$got = $false
foreach ($attempt in 1..2) {
    foreach ($url in $mirrors) {
        $host_ = ([Uri]$url).Host
        try {
            Write-Host "  downloading TinyCC from $host_ (attempt $attempt)..." -ForegroundColor Cyan
            Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing -TimeoutSec 120

            $size = (Get-Item $zip).Length
            if ($size -ne $expectedSize) { throw "got $size bytes, expected $expectedSize" }

            $hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
            if ($hash -ne $expectedHash) { throw "sha-256 mismatch: $hash" }

            $got = $true
            break
        } catch {
            Write-Host "    $host_ : $($_.Exception.Message)" -ForegroundColor DarkYellow
            if (Test-Path $zip) { Remove-Item $zip -Force }
        }
    }
    if ($got) { break }
    if ($attempt -lt 2) { Start-Sleep -Seconds 5 }
}

if (-not $got) {
    throw "could not download TinyCC from any mirror. Check your connection, or fetch tcc-0.9.27-win64-bin.zip by hand and unzip it to $dest"
}

Write-Host "  verified, extracting..." -ForegroundColor Cyan
Expand-Archive -Path $zip -DestinationPath $here -Force
Remove-Item $zip -Force

if (-not (Test-Path (Join-Path $dest 'tcc.exe'))) {
    throw "extracted, but $dest\tcc.exe is missing"
}

& (Join-Path $dest 'tcc.exe') -v
Write-Host "  ready. Now run build.cmd" -ForegroundColor Green
