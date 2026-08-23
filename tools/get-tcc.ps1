# Fetches the compiler. Run once; build.cmd does the rest.
#
# TinyCC is a 480 KB download that unzips to 1.6 MB, needs no installer, and
# leaves nothing behind if you delete tools\tcc. It is the whole toolchain.

$ErrorActionPreference = 'Stop'

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$dest = Join-Path $here 'tcc'
$zip  = Join-Path $here 'tcc.zip'
$url  = 'https://download.savannah.gnu.org/releases/tinycc/tcc-0.9.27-win64-bin.zip'

if (Test-Path (Join-Path $dest 'tcc.exe')) {
    Write-Host "  already present: $dest" -ForegroundColor DarkGray
    & (Join-Path $dest 'tcc.exe') -v
    exit 0
}

[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

Write-Host "  downloading TinyCC..." -ForegroundColor Cyan
Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing

Write-Host "  extracting..." -ForegroundColor Cyan
Expand-Archive -Path $zip -DestinationPath $here -Force
Remove-Item $zip -Force

if (-not (Test-Path (Join-Path $dest 'tcc.exe'))) {
    throw "extracted, but $dest\tcc.exe is missing"
}

& (Join-Path $dest 'tcc.exe') -v
Write-Host "  ready. Now run build.cmd" -ForegroundColor Green
