<#
.SYNOPSIS
  Travels through the campaign chapter by chapter and collects the crashes, so missing function
  hints (hints\runtime.toml) are found in one pass instead of one boot at a time.

.DESCRIPTION
  For each chapter in -From..-To it writes `servertravel geargame_p?loadcheckpoint=0?chapter=<n-1>`
  to the game's dev command file (docs/TEST_TOOLING.md, "Chapter travel"), watches the log for
  -Seconds and records the outcome in <OutDir>\results.tsv:

    ok                          the game was still running and had logged no FATAL
    unregistered 0x82XXXXXX     "Call to invalid or unregistered function": add the address to
                                hints\runtime.toml (the harvest tool does it: -AddHints)
    exit / fatal                the process ended or logged another FATAL; the last log lines are kept

  The game is restarted after a crash and the sweep goes on with the next chapter. Chapter travel
  reaches a chapter's first minutes only: code that combat, vehicles or bosses use is not tried.

.PARAMETER GameFolder
  Your game folder (default: gow2_game_path.txt next to gow2.exe, then ..\game-data).

.PARAMETER AddHints
  Appends every new "unregistered" address to hints\runtime.toml. Run scripts\build.ps1 afterwards
  (it regenerates the code, about a minute, then compiles what changed).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\chapter-sweep.ps1 -GameFolder C:\Games\GoW2 -AddHints
#>
param(
    [int]$From = 1,
    [int]$To = 29,
    [int]$Seconds = 75,
    [string]$GameFolder = '',
    [string]$Exe = '',
    [string]$OutDir = '',
    [switch]$AddHints
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $Exe) { $Exe = Join-Path $root 'out\build\release\gow2.exe' }
if (-not $OutDir) { $OutDir = Join-Path $root 'out\sweep' }
if (-not $GameFolder) {
    $saved = Join-Path (Split-Path $Exe) 'gow2_game_path.txt'
    if (Test-Path $saved) { $GameFolder = (Get-Content $saved -Raw).Trim() }
}
if (-not $GameFolder) { throw 'Give your game folder with -GameFolder.' }
if (-not (Test-Path $Exe)) { throw "$Exe not found: run scripts\build.ps1 first." }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$results = Join-Path $OutDir 'results.tsv'
if (-not (Test-Path $results)) { "chapter`toutcome`taddress`tlog" | Set-Content $results }
$cmd = Join-Path $OutDir 'cmd.txt'
$hints = Join-Path $root 'hints\runtime.toml'
$known = @{}
if (Test-Path $hints) {
    Select-String -Path $hints -Pattern '^(0x[0-9A-Fa-f]{8})' | ForEach-Object { $known[$_.Matches[0].Groups[1].Value.ToUpper()] = $true }
}
$script:run = 0
$script:proc = $null
$script:log = ''

function Start-Game {
    $script:run++
    $script:log = Join-Path $OutDir ("run{0:D3}.log" -f $script:run)
    [IO.File]::WriteAllText($cmd, '')
    $script:proc = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -PassThru -ArgumentList @(
        "--game_data_root=$GameFolder", "--gow2_dev_command_file=$cmd", "--log_file=$($script:log)")
    $deadline = (Get-Date).AddSeconds(180)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 3
        if ($script:proc.HasExited) { return $false }
        if ((Test-Path $script:log) -and (Select-String -Path $script:log -Pattern 'DEVCMD on:' -Quiet)) { return $true }
    }
    return $false
}

function Stop-Game {
    if ($script:proc -and -not $script:proc.HasExited) { Stop-Process -Id $script:proc.Id -Force }
    $script:proc = $null
}

function Outcome {
    # $null while all is well, else @(outcome, address).
    $text = if (Test-Path $script:log) { Get-Content $script:log -Raw -ErrorAction SilentlyContinue } else { '' }
    if ($text -match 'unregistered function at guest address (0x[0-9A-Fa-f]{8})') { return @('unregistered', $Matches[1].ToUpper()) }
    if ($text -match '\[fatal\]|FATAL') { return @('fatal', '') }
    if ($script:proc.HasExited) { return @('exit', '') }
    return $null
}

Write-Host "Sweeping chapters $From..$To, $Seconds s each. Results: $results"
if (-not (Start-Game)) { throw 'The game did not start (see the first log in the output folder).' }
for ($n = $From; $n -le $To; $n++) {
    if (-not $script:proc -or $script:proc.HasExited) {
        if (-not (Start-Game)) { throw 'The game did not restart.' }
    }
    $mark = (Get-Content $script:log -ErrorAction SilentlyContinue | Measure-Object -Line).Lines
    [IO.File]::AppendAllText($cmd, "servertravel geargame_p?loadcheckpoint=0?chapter=$($n - 1)`n")
    $end = (Get-Date).AddSeconds($Seconds)
    $found = $null
    while ((Get-Date) -lt $end -and -not $found) {
        Start-Sleep -Seconds 3
        $found = Outcome
    }
    if ($found) {
        $outcome, $addr = $found
        $tail = (Get-Content $script:log -Tail 4 -ErrorAction SilentlyContinue) -join ' | '
        "{0}`t{1}`t{2}`t{3}" -f $n, $outcome, $addr, $script:log | Add-Content $results
        Write-Host ("chapter {0}: {1} {2}" -f $n, $outcome, $addr) -ForegroundColor Yellow
        if ($outcome -eq 'unregistered' -and $AddHints -and -not $known.ContainsKey($addr)) {
            if (-not (Test-Path $hints)) {
                "# Found by booting the game (FATAL: call to unregistered function).`n[functions]" | Set-Content $hints
            }
            "$addr = {}  # chapter sweep, chapter $n" | Add-Content $hints
            $known[$addr] = $true
        }
        Stop-Game
    } else {
        "{0}`tok`t`t{1}" -f $n, $script:log | Add-Content $results
        Write-Host "chapter ${n}: ok"
    }
}
Stop-Game
Write-Host "Done. $results"
