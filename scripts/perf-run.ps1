<#
.SYNOPSIS
  Runs the performance scenes (scripts\perf\scenes.json) against gow2.exe and reports frame pacing,
  GPU time and hitches, optionally against a saved baseline.

.DESCRIPTION
  For each scene and repetition: starts the game with the per-frame log on (perf_frame_csv, SDK patch
  0017), travels to the scene's chapter, waits for it to load, runs the scene's pad route for its
  duration, stops the game and analyses only that stretch (tools\perf\analyze.py). The repetitions
  are combined into a median (tools\perf\summarize.py).

  Output goes to out\perf\<stamp>_<tag>\<scene>\run<n>\ (frames.csv, game log, metrics.json,
  report.txt, run.json) and <scene>\summary.json. Baselines live in perf\baselines\<scene>.json
  (numbers only, no game data).

  The game is left alone while a scene runs: do not touch the keyboard or mouse, and close other
  heavy programs. Sizes of change smaller than the spread between repetitions are not results.

.PARAMETER Scene
  Scene names, or 'all'. Default: hospital_idle, hospital_walk.

.PARAMETER Repeat
  Runs per scene (default 3).

.PARAMETER Set
  Game settings for this run, as name=value (for example vsync=false resolution_scale=1). They are
  passed as --name=value, so they win over gow2.toml; they are recorded in run.json.

.PARAMETER Tag
  Label for the output folder (default: none).

.PARAMETER Compare
  Compare each scene's summary with perf\baselines\<scene>.json; exit status 2 when something is worse.

.PARAMETER SaveBaseline
  Write each scene's summary to perf\baselines\<scene>.json.

.PARAMETER Cold
  Start every run with an empty shader and pipeline cache (first-visit stutter), in a temporary folder.

.PARAMETER Interactive
  No scene: starts the game with the log on and records while you play, until you quit the game or
  -Seconds pass. The report covers the stretch after -SkipSeconds.

.PARAMETER Desktop
  Virtual desktop (1-based) the game window is moved to, so you can keep working on yours (default 1;
  0 = leave it where it opens). Needs the VirtualDesktop module (Install-Module VirtualDesktop
  -Scope CurrentUser); without it the window stays where it opens. Switch to that desktop if a
  scene looks different from a visible run: a window nobody sees may be paced differently.

.PARAMETER GameFolder
  Your game folder (default: gow2_game_path.txt next to the exe).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\perf-run.ps1 -Scene hospital_idle -Set vsync=false -Tag nolimit
  powershell -ExecutionPolicy Bypass -File scripts\perf-run.ps1 -Scene all -Compare
  powershell -ExecutionPolicy Bypass -File scripts\perf-run.ps1 -Interactive -Seconds 600 -SkipSeconds 60
#>
param(
    [string[]]$Scene = @('hospital_idle', 'hospital_walk'),
    [int]$Repeat = 3,
    [string[]]$Set = @(),
    [string]$Tag = '',
    [switch]$Compare,
    [switch]$SaveBaseline,
    [switch]$Cold,
    [switch]$Interactive,
    [int]$Seconds = 600,
    [int]$SkipSeconds = 30,
    [int]$Desktop = 1,
    [string]$GameFolder = '',
    [string]$Exe = '',
    [string]$OutRoot = ''
)

# With -File a comma list arrives as one string.
$Scene = @($Scene | % { $_ -split ',' } | ? { $_ })
$Set = @($Set | % { $_ -split ',' } | ? { $_ })
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $Exe) { $Exe = Join-Path $root 'out\build\release\gow2.exe' }
if (-not $OutRoot) { $OutRoot = Join-Path $root 'out\perf' }
if (-not (Test-Path $Exe)) { throw "$Exe not found: run scripts\build.ps1 first." }
$exeDir = Split-Path -Parent (Resolve-Path $Exe)
if (-not $GameFolder) {
    $saved = Join-Path $exeDir 'gow2_game_path.txt'
    if (Test-Path $saved) { $GameFolder = (Get-Content $saved -Raw).Trim() }
}
if (-not $GameFolder) { throw 'Give your game folder with -GameFolder.' }
$py = if (Get-Command py -ErrorAction SilentlyContinue) { 'py' } else { 'python' }
$analyze = Join-Path $root 'tools\perf\analyze.py'
$summarize = Join-Path $root 'tools\perf\summarize.py'
$baselines = Join-Path $root 'perf\baselines'
$stamp = (Get-Date).ToString('yyyyMMdd-HHmmss') + $(if ($Tag) { "_$Tag" } else { '' })
$runRoot = Join-Path $OutRoot $stamp
New-Item -ItemType Directory -Force $runRoot | Out-Null

function Wall-Ms { [int64]([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()) }

function Gpu-Info {
    $smi = Get-Command nvidia-smi -ErrorAction SilentlyContinue
    if (-not $smi) { return $null }
    $line = & nvidia-smi --query-gpu=name,driver_version,clocks.max.sm --format=csv,noheader 2>$null
    if ($line) { return ($line | Select -First 1).Trim() }
    return $null
}

function Gpu-Sample {
    $smi = Get-Command nvidia-smi -ErrorAction SilentlyContinue
    if (-not $smi) { return $null }
    $v = & nvidia-smi --query-gpu=utilization.gpu,clocks.sm,power.draw,temperature.gpu --format=csv,noheader,nounits 2>$null
    if (-not $v) { return $null }
    $p = ($v | Select -First 1).Split(',') | % { [double]$_.Trim() }
    return [pscustomobject]@{ util = $p[0]; sm_mhz = $p[1]; watts = $p[2]; temp_c = $p[3] }
}

function Start-Game($dir, $extraArgs) {
    New-Item -ItemType Directory -Force $dir | Out-Null
    $script:cmdFile = Join-Path $dir 'cmd.txt'
    $script:padFile = Join-Path $dir 'pad.txt'
    [IO.File]::WriteAllText($script:cmdFile, '')
    [IO.File]::WriteAllText($script:padFile, '')
    $script:logFile = Join-Path $dir 'game.log'
    $script:csvFile = Join-Path $dir 'frames.csv'
    $args2 = @("--game_data_root=$GameFolder", "--gow2_dev_command_file=$script:cmdFile",
        "--gow2_test_input=$script:padFile", "--log_file=$script:logFile", "--perf_frame_csv=$script:csvFile")
    foreach ($s in $Set) { $args2 += "--$s" }
    $args2 += $extraArgs
    $script:proc = Start-Process -FilePath $Exe -ArgumentList $args2 -WorkingDirectory $exeDir -PassThru
}

# Moves the game window to virtual desktop $Desktop (best effort: a missing module or window only warns).
function Move-GameWindow {
    if ($Desktop -lt 1) { return }
    if (-not (Get-Module -ListAvailable VirtualDesktop)) {
        Write-Warning 'VirtualDesktop module not installed: the game stays on the current desktop.'; $script:Desktop = 0; return
    }
    Import-Module VirtualDesktop -WarningAction SilentlyContinue
    if ($Desktop -gt (Get-DesktopCount)) { Write-Warning "There is no desktop $Desktop."; $script:Desktop = 0; return }
    $target = Get-Desktop ($Desktop - 1)
    $end = (Get-Date).AddSeconds(90)
    while ((Get-Date) -lt $end -and -not $script:proc.HasExited) {
        $script:proc.Refresh()
        $h = $script:proc.MainWindowHandle
        if ($h -ne [IntPtr]::Zero) {
            if ((Get-DesktopIndex (Get-DesktopFromWindow $h)) -ne ($Desktop - 1)) { Move-Window $target $h | Out-Null }
            return
        }
        Start-Sleep -Milliseconds 500
    }
    Write-Warning 'No game window to move.'
}

function Wait-For-Log($pattern, $timeout) {
    $end = (Get-Date).AddSeconds($timeout)
    while ((Get-Date) -lt $end) {
        if ($script:proc.HasExited) { throw 'The game exited while starting.' }
        if ((Test-Path $script:logFile) -and (Select-String -Path $script:logFile -Pattern $pattern -Quiet)) { return }
        Start-Sleep -Seconds 2
    }
    throw "Timed out waiting for '$pattern' in the game log."
}

function Stop-Game {
    if ($script:proc -and -not $script:proc.HasExited) { Stop-Process -Id $script:proc.Id -Force }
    Start-Sleep -Seconds 2
}

function Write-Pad($text) { [IO.File]::WriteAllText($script:padFile, $text + "`n") }

function Run-Analysis($dir, $fromMs, $toMs) {
    $metrics = Join-Path $dir 'metrics.json'
    $analysisArgs = @($analyze, (Join-Path $dir 'frames.csv'), '--json', $metrics)
    if ($fromMs) { $analysisArgs += @('--from-wall-ms', $fromMs) }
    if ($toMs) { $analysisArgs += @('--to-wall-ms', $toMs) }
    $text = & $py @analysisArgs 2>&1 | Out-String
    Set-Content (Join-Path $dir 'report.txt') $text -Encoding utf8
    return $metrics
}

function Write-RunInfo($dir, $scene, $fromMs, $toMs, $gpuSamples) {
    $sha = (Get-FileHash $Exe -Algorithm SHA256).Hash
    $commit = ''
    try { $commit = (git -C $root rev-parse --short HEAD 2>$null) } catch {}
    $gpu = if ($gpuSamples.Count) {
        [pscustomobject]@{
            util_avg = [math]::Round(($gpuSamples | Measure-Object util -Average).Average, 1)
            sm_mhz_avg = [math]::Round(($gpuSamples | Measure-Object sm_mhz -Average).Average, 0)
            watts_avg = [math]::Round(($gpuSamples | Measure-Object watts -Average).Average, 0)
            temp_c_max = ($gpuSamples | Measure-Object temp_c -Maximum).Maximum
        }
    } else { $null }
    [pscustomobject]@{
        scene = $scene; exe_sha256 = $sha; git_commit = $commit; settings = $Set; cold = [bool]$Cold; desktop = $Desktop
        gpu = Gpu-Info; gpu_samples = $gpu
        cpu = (Get-CimInstance Win32_Processor | Select -First 1 -Expand Name)
        from_wall_ms = $fromMs; to_wall_ms = $toMs
        toml_sha256 = (Get-FileHash (Join-Path $exeDir 'gow2.toml') -Algorithm SHA256 -ErrorAction SilentlyContinue).Hash
    } | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $dir 'run.json') -Encoding utf8
}

if ($Interactive) {
    $dir = Join-Path $runRoot 'interactive'
    Start-Game $dir @()
    Move-GameWindow
    Write-Host "Recording. Play, then quit the game (or wait $Seconds s). Log: $script:csvFile"
    $started = Wall-Ms
    $gpuSamples = @()
    $end = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $end -and -not $script:proc.HasExited) {
        Start-Sleep -Seconds 5
        $g = Gpu-Sample; if ($g) { $gpuSamples += $g }
    }
    Stop-Game
    $from = $started + 1000 * $SkipSeconds
    Write-RunInfo $dir 'interactive' $from (Wall-Ms) $gpuSamples
    $m = Run-Analysis $dir $from $null
    Get-Content (Join-Path $dir 'report.txt')
    exit 0
}

$sceneFile = Join-Path $PSScriptRoot 'perf\scenes.json'
$all = (Get-Content $sceneFile -Raw | ConvertFrom-Json).scenes
if ($Scene -contains 'all') { $Scene = @($all.PSObject.Properties.Name) }
$worst = 0
foreach ($name in $Scene) {
    $def = $all.$name
    if (-not $def) { throw "No scene '$name' in $sceneFile." }
    $metricFiles = @()
    for ($i = 1; $i -le $Repeat; $i++) {
        $dir = Join-Path $runRoot "$name\run$i"
        Write-Host "== $name, run $i of $Repeat" -ForegroundColor Cyan
        $extra = @()
        if ($Cold) { $extra += "--cache_root=$(Join-Path $dir 'cache')" }
        Start-Game $dir $extra
        $gpuSamples = @()
        try {
            Move-GameWindow
            Wait-For-Log 'DEVCMD on:' 240
            Move-GameWindow  # the window can be recreated while the game starts
            [IO.File]::AppendAllText($script:cmdFile, "servertravel geargame_p?loadcheckpoint=0?chapter=$([int]$def.chapter - 1)`n")
            $from = Wall-Ms
            if ($def.load_seconds -gt 0) {
                Start-Sleep -Seconds $def.load_seconds
                $from = Wall-Ms
            }
            $stepList = @($def.steps)
            $next = 0
            $sceneStart = [DateTime]::UtcNow
            while ($true) {
                $t = ([DateTime]::UtcNow - $sceneStart).TotalSeconds
                if ($t -ge $def.duration -or $script:proc.HasExited) { break }
                while ($next -lt $stepList.Count -and $t -ge $stepList[$next].at) {
                    Write-Pad $stepList[$next].pad
                    $next++
                }
                if ([int]$t % 5 -eq 0) { $g = Gpu-Sample; if ($g) { $gpuSamples += $g } }
                Start-Sleep -Milliseconds 250
            }
            $to = Wall-Ms
            if ($script:proc.HasExited) { Write-Host '   the game exited during the scene' -ForegroundColor Yellow }
        } finally {
            Write-Pad ''
            Stop-Game
        }
        Write-RunInfo $dir $name $from $to $gpuSamples
        $m = Run-Analysis $dir $from $to
        Get-Content (Join-Path $dir 'report.txt') | Select -First 4 | % { "   $_" }
        $metricFiles += $m
    }
    $summary = Join-Path $runRoot "$name\summary.json"
    $summaryArgs = @($summarize) + $metricFiles + @('--json', $summary)
    $baseFile = Join-Path $baselines "$name.json"
    if ($Compare) { $summaryArgs += @('--baseline', $baseFile) }
    & $py @summaryArgs | Tee-Object -FilePath (Join-Path $runRoot "$name\summary.txt")
    if ($LASTEXITCODE -eq 2) { $worst = 2 }
    if ($SaveBaseline) {
        New-Item -ItemType Directory -Force $baselines | Out-Null
        Copy-Item $summary $baseFile -Force
        Write-Host "   baseline saved: $baseFile" -ForegroundColor Green
    }
}
Write-Host "`nResults: $runRoot"
exit $worst
