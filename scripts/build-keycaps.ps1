<#
.SYNOPSIS
  Builds the keycap texture pack (the keyboard prompts drawn as keys) from your own game.

.DESCRIPTION
  The game draws its button prompts with one font; two 256x256 pages hold the Xbox buttons and
  the spinner. tools\keycaps\pack.py draws a keycap for every key a keybind can name into the free
  space of those pages and keeps the glyphs the game still needs. The pack holds game art, so build
  it yourself and do not share it. Steps:

    1. installs the Python packages the tool needs (pillow numpy resvg-py fonttools, for your user),
    2. when your texture dump lacks the two font pages, starts gow2.exe with texture_dump on. Play
       to the difficulty screen (Solo Campaign), then quit the game,
    3. writes textures\4D53082D\replace\*.dds and keycaps.id next to gow2.exe.

  After that gow2.exe shows keycaps (gow2_keycaps = true, the default) and switches to the pad's
  buttons while a controller is connected. The pack must match the exe's keycap table: run this
  again after a build that changes tools\keycaps or src\gow2_keycap_layout.inc.

.PARAMETER Exe
  The game exe (default out\build\release\gow2.exe).

.PARAMETER GameFolder
  Your game folder, passed to the game when it has to be started to dump the pages.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\build-keycaps.ps1 -GameFolder C:\Games\GoW2
#>
param(
    [string]$Exe = '',
    [string]$GameFolder = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
if (-not $Exe) { $Exe = Join-Path $root 'out\build\release\gow2.exe' }
function Fail($msg) { Write-Host "ERROR: $msg" -ForegroundColor Red; exit 1 }
function Step($msg) { Write-Host "`n== $msg" -ForegroundColor Cyan }

if (-not (Test-Path $Exe)) { Fail "$Exe not found: run scripts\build.ps1 first." }
if (-not (Get-Command py -ErrorAction SilentlyContinue)) { Fail 'Python 3 (the py launcher) was not found.' }
$exeDir = Split-Path -Parent (Resolve-Path $Exe)
$pack = Join-Path $exeDir 'textures\4D53082D'
$dump = Join-Path $pack 'dump'
$replace = Join-Path $pack 'replace'

Step 'Python packages'
py -m pip install --user --quiet pillow numpy resvg-py fonttools
if ($LASTEXITCODE -ne 0) { Fail 'pip could not install the Python packages.' }

$info = Get-Content (Join-Path $root 'tools\keycaps\font_info.json') -Raw | ConvertFrom-Json
function Missing-Pages {
    $info.page_hashes | Where-Object { -not (Get-ChildItem $dump -Filter "$_`_*.dds" -ErrorAction SilentlyContinue) }
}

if (Missing-Pages) {
    Step 'Dumping the font pages'
    Write-Host 'gow2.exe starts now. Press Start, choose Solo Campaign and wait for the Select Difficulty'
    Write-Host 'screen (its spinner draws the second font page), then quit the game.'
    $arguments = @('--texture_dump=true', '--gow2_keycaps=false', '--gow2_prompts=controller')
    if ($GameFolder) { $arguments += "--game_data_root=$GameFolder" }
    Start-Process -FilePath $Exe -ArgumentList $arguments -WorkingDirectory $exeDir -Wait
    $missing = @(Missing-Pages)
    if ($missing.Count) { Fail "The dump still lacks page(s) $($missing -join ', '). Reach the Select Difficulty screen and run this again." }
}

Step 'Building the pack'
py (Join-Path $root 'tools\keycaps\pack.py') --dump $dump --out $replace
if ($LASTEXITCODE -ne 0) { Fail 'pack.py failed.' }
Write-Host "`nDone: $replace" -ForegroundColor Green
Write-Host 'Start gow2.exe; the prompts follow your keybinds and the connected controller.'
