# Runs several gow2.exe instances on one PC for co-op tests: each with its own user data folder,
# log file and window slot. One action per call, so a test can look at a screenshot before it
# decides the next key. State lives in <OutDir>\instances.json.
#
#   coop-instances.ps1 start -Count 2 -OutDir out\coop-test\s1 -Common "--net_bind_ip=127.0.0.{i}"
#   coop-instances.ps1 shot  -Instance 2 -Label menu -OutDir out\coop-test\s1
#   coop-instances.ps1 key   -Instance 2 -Key Space -OutDir out\coop-test\s1
#   coop-instances.ps1 pad   -Instance 2 -Pad "A" -Hold 200 -OutDir out\coop-test\s1
#   coop-instances.ps1 hold  -Instance 2 -Pad "LY=1" -OutDir out\coop-test\s1   (release clears it)
#   coop-instances.ps1 stats -Seconds 10 -OutDir out\coop-test\s1
#   coop-instances.ps1 stop  -OutDir out\coop-test\s1
#
# key/down/up send real keystrokes to the focused window, so they need the focus. pad/hold/release
# write the instance's gow2_test_input file instead: the game reads it as guest user 0's pad even
# without focus, which is what lets several instances, or one behind a system dialog, be driven.
# -PerInstance lists each instance's own flags separated by '|' (first part for instance 1).
# {i} in -Common or -PerInstance becomes the instance number (1-based). -SeedUser copies a user
# data folder (saves and profile) into each new instance folder and -SeedCache a cache folder
# (shaders, edited Coalesced bundles) into its user\cache; the sources are only read. start also
# runs the idle guard (-Guard, the main checkout's out\idle-gate\guard-user.ps1) unless -NoGuard,
# and stop ends it and exits 3 when the guard saw the user come back.
param(
    [Parameter(Mandatory, Position = 0)]
    [ValidateSet('start', 'shot', 'shots', 'key', 'down', 'up', 'pad', 'hold', 'release', 'stats', 'status', 'stop')]
    [string]$Action,
    [Parameter(Mandatory)] [string]$OutDir,
    [int]$Count = 1,
    [int]$Instance = 1,
    [string]$Key = '',
    [string]$Pad = '',
    [int]$Hold = 120,
    [string]$Label = 'shot',
    [string]$Common = '',
    [string]$PerInstance = '',
    [string]$SeedUser = '',
    [string]$SeedCache = '',
    [int]$Seconds = 10,
    [int]$Width = 800,
    [int]$Height = 450,
    [int]$StaggerSec = 10,
    [switch]$NoGuard,
    [string]$GameRoot = 'C:\personal\gow2-gamedata\retail',
    [string]$Exe = '',
    [string]$Guard = 'C:\personal\gow2-recomp\out\idle-gate\guard-user.ps1'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $Exe) { $Exe = Join-Path $root 'out\build\release\gow2.exe' }
if (-not [IO.Path]::IsPathRooted($OutDir)) { $OutDir = Join-Path $root $OutDir }
$stateFile = Join-Path $OutDir 'instances.json'
$guardFlag = Join-Path (Split-Path $Guard) 'user-returned.txt'

Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class CoopWin {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr pid);
  [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool attach);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint mapType);
}
'@

function Read-State {
    if (-not (Test-Path $stateFile)) { return @() }
    return @(Get-Content $stateFile -Raw | ConvertFrom-Json)
}

function Write-State($items) { ConvertTo-Json -InputObject @($items) -Depth 4 | Set-Content $stateFile }

function Expand-InstanceArgs([string]$text, [int]$i) { ($text -replace '\{i\}', "$i") -split ' ' | Where-Object { $_ } }

function New-InstanceFolder([int]$i) {
    $dir = Join-Path $OutDir "inst$i"
    $user = Join-Path $dir 'user'
    if ($SeedUser -and -not (Test-Path $user)) { Copy-Item -Recurse -Path $SeedUser -Destination $user }
    New-Item -ItemType Directory -Force -Path $user | Out-Null
    Set-Content -Path (Join-Path $dir 'pad.txt') -Value ''
    $cache = Join-Path $user 'cache'
    if ($SeedCache -and -not (Test-Path $cache)) { Copy-Item -Recurse -Path $SeedCache -Destination $cache }
    return $dir
}

function Get-InstanceArgs([int]$i, [string]$dir) {
    $own = @("--game_data_root=`"$GameRoot`"", "--log_file=`"$(Join-Path $dir 'game.log')`"",
        "--gow2_test_input=`"$(Join-Path $dir 'pad.txt')`"",
        "--user_data_root=`"$(Join-Path $dir 'user')`"", '--fullscreen=false',
        "--window_width=$Width", "--window_height=$Height", '--resolution_scale=1')
    $extra = @(Expand-InstanceArgs $Common $i)
    $ownExtra = @($PerInstance -split '\|')
    if ($ownExtra.Count -ge $i) { $extra += Expand-InstanceArgs $ownExtra[$i - 1] $i }
    return $own + $extra
}

function Wait-Window($proc, [int]$timeoutSec = 60) {
    $clock = [Diagnostics.Stopwatch]::StartNew()
    while ($clock.Elapsed.TotalSeconds -lt $timeoutSec -and -not $proc.HasExited) {
        $proc.Refresh()
        if ($proc.MainWindowHandle -ne [IntPtr]::Zero) { return $proc.MainWindowHandle }
        Start-Sleep -Milliseconds 250
    }
    return [IntPtr]::Zero
}

# Slot i of a grid as wide as the main screen allows, from its top left, so no window covers another.
function Move-ToSlot([IntPtr]$h, [int]$i) {
    $r = New-Object CoopWin+RECT
    [CoopWin]::GetWindowRect($h, [ref]$r) | Out-Null
    $w = $r.R - $r.L; $hh = $r.B - $r.T
    $columns = [Math]::Max(1, [Math]::Floor([System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea.Width / $w))
    $x = (($i - 1) % $columns) * $w; $y = [Math]::Floor(($i - 1) / $columns) * $hh
    # SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
    [CoopWin]::SetWindowPos($h, [IntPtr]::Zero, $x, $y, 0, 0, 0x15) | Out-Null
}

function Start-Guard {
    if ($NoGuard) { return $null }
    $g = Start-Process powershell -WindowStyle Hidden -PassThru -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $Guard
    Start-Sleep -Seconds 2
    return $g
}

function Get-StartTicks($proc) { if ($proc) { $proc.StartTime.Ticks } else { 0 } }

# A process id can be reused once its process is gone, so only the process that started then dies.
function Stop-SameProcess([int]$id, [long]$startTicks) {
    $p = Get-Process -Id $id -ErrorAction SilentlyContinue
    if ($p -and $p.StartTime.Ticks -eq $startTicks) { Stop-Process -Id $id -Force -ErrorAction SilentlyContinue }
}

function Start-Instances {
    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
    $items = @()
    $guard = Start-Guard
    for ($i = 1; $i -le $Count; $i++) {
        $dir = New-InstanceFolder $i
        $proc = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -PassThru -ArgumentList (Get-InstanceArgs $i $dir)
        $h = Wait-Window $proc
        if ($h -ne [IntPtr]::Zero) { Move-ToSlot $h $i }
        $items += [pscustomobject]@{ Index = $i; Pid = $proc.Id; Ticks = (Get-StartTicks $proc); Dir = $dir
            Guard = $(if ($guard) { $guard.Id } else { 0 }); GuardTicks = (Get-StartTicks $guard) }
        Write-State $items
        Write-Output "instance $i pid $($proc.Id) log $(Join-Path $dir 'game.log')"
        if ($i -lt $Count) { Start-Sleep -Seconds $StaggerSec }
    }
}

function Get-InstanceProcess([int]$i) {
    $item = Read-State | Where-Object { $_.Index -eq $i }
    if (-not $item) { throw "no instance $i in $stateFile" }
    $proc = Get-Process -Id $item.Pid -ErrorAction SilentlyContinue
    if (-not $proc -or $proc.ProcessName -ne 'gow2') { throw "instance $i (pid $($item.Pid)) is not running" }
    return $proc
}

function Request-Foreground([IntPtr]$h) {
    $fg = [CoopWin]::GetForegroundWindow()
    $me = [CoopWin]::GetCurrentThreadId()
    $other = [CoopWin]::GetWindowThreadProcessId($fg, [IntPtr]::Zero)
    $attached = ($other -ne 0) -and [CoopWin]::AttachThreadInput($me, $other, $true)
    [CoopWin]::BringWindowToTop($h) | Out-Null
    [CoopWin]::SetForegroundWindow($h) | Out-Null
    if ($attached) { [CoopWin]::AttachThreadInput($me, $other, $false) | Out-Null }
    Start-Sleep -Milliseconds 150
    return ([CoopWin]::GetForegroundWindow() -eq $h)
}

function Invoke-UnboundKeyThenRequest([IntPtr]$h) {
    Send-KeyEvent 'F24' 0; Send-KeyEvent 'F24' 2
    return (Request-Foreground $h)
}

# Windows lets the process that sent the last input take the foreground; an unbound key (F24)
# gives it that right when there is no foreground thread to attach to. Nothing gets past a system
# dialog such as the Windows Firewall prompt (it dims the screen and keeps the foreground): use the
# pad actions then.
function Set-Focus([IntPtr]$h) {
    if ([CoopWin]::GetForegroundWindow() -eq $h) { return $true }
    if (Request-Foreground $h) { return $true }
    return (Invoke-UnboundKeyThenRequest $h)
}

function Get-ForegroundOwner {
    $fg = [CoopWin]::GetForegroundWindow()
    if ($fg -eq [IntPtr]::Zero) { return 'no window' }
    $p = Get-Process | Where-Object { $_.MainWindowHandle -eq $fg } | Select-Object -First 1
    if ($p) { return "$($p.ProcessName) $($p.Id)" }
    return "window $fg"
}

function Get-Vk([string]$name) { [byte][System.Windows.Forms.Keys]::$name }
function Get-Scan([string]$name) { [byte][CoopWin]::MapVirtualKey([uint32](Get-Vk $name), 0) }
# KEYEVENTF_EXTENDEDKEY for the navigation keys, as a physical keyboard sends them.
function Get-KeyFlags([string]$name) { if ($name -in 'Left', 'Right', 'Up', 'Down', 'Home', 'End', 'Insert', 'Delete', 'PageUp', 'PageDown') { 1 } else { 0 } }

function Send-KeyEvent([string]$name, [uint32]$up) {
    [CoopWin]::keybd_event((Get-Vk $name), (Get-Scan $name), ((Get-KeyFlags $name) -bor $up), [UIntPtr]::Zero)
}

# Keys go to the foreground window, so nothing is sent unless the instance really has the focus.
function Send-InstanceKey([string]$mode) {
    $proc = Get-InstanceProcess $Instance
    if (-not (Set-Focus $proc.MainWindowHandle)) { throw "instance $Instance did not get the focus ($(Get-ForegroundOwner) has it); key not sent" }
    if ($mode -ne 'up') { Send-KeyEvent $Key 0 }
    if ($mode -eq 'key') { Start-Sleep -Milliseconds $Hold }
    if ($mode -ne 'down') { Send-KeyEvent $Key 2 }
    Write-Output "$(Get-Date -Format 'HH:mm:ss.fff') instance $Instance $mode $Key"
}

function Write-PadFile([int]$i, [string]$line) {
    $path = Join-Path (Join-Path $OutDir "inst$i") 'pad.txt'
    for ($try = 0; $try -lt 20; $try++) {
        try {
            $fs = [IO.File]::Open($path, [IO.FileMode]::Create, [IO.FileAccess]::Write, [IO.FileShare]::ReadWrite)
            $bytes = [Text.Encoding]::ASCII.GetBytes($line + "`n")
            $fs.Write($bytes, 0, $bytes.Length); $fs.Dispose()
            return
        } catch [IO.IOException] { Start-Sleep -Milliseconds 5 }
    }
    throw "could not write $path"
}

function Send-InstancePad([string]$mode) {
    Get-InstanceProcess $Instance | Out-Null
    if ($mode -eq 'release') { Write-PadFile $Instance ''; Write-Output "$(Get-Date -Format 'HH:mm:ss.fff') instance $Instance pad released"; return }
    Write-PadFile $Instance $Pad
    if ($mode -eq 'pad') { Start-Sleep -Milliseconds $Hold; Write-PadFile $Instance '' }
    Write-Output "$(Get-Date -Format 'HH:mm:ss.fff') instance $Instance $mode '$Pad'"
}

# No focus change: the windows sit in separate slots, so the screen shows each one.
function Save-InstanceShot([int]$i) {
    $proc = Get-InstanceProcess $i
    $h = $proc.MainWindowHandle
    $r = New-Object CoopWin+RECT
    [CoopWin]::GetWindowRect($h, [ref]$r) | Out-Null
    $bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
    $path = Join-Path $OutDir ("{0}-i{1}-{2}.png" -f (Get-Date -Format 'HHmmss'), $i, $Label)
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    Write-Output "shot $path"
}

function Get-GpuMemoryMB([int]$id) {
    $c = Get-Counter "\GPU Process Memory(pid_$($id)_*)\Dedicated Usage" -ErrorAction SilentlyContinue
    if (-not $c) { return 0 }
    return [Math]::Round(($c.CounterSamples | Measure-Object CookedValue -Sum).Sum / 1MB)
}

function Get-Gpu3dPercent([int]$id) {
    $c = Get-Counter "\GPU Engine(pid_$($id)_*engtype_3D)\Utilization Percentage" -ErrorAction SilentlyContinue
    if (-not $c) { return 0 }
    return [Math]::Round(($c.CounterSamples | Measure-Object CookedValue -Sum).Sum, 1)
}

function Write-Stats {
    $live = Read-State | ForEach-Object { Get-Process -Id $_.Pid -ErrorAction SilentlyContinue } | Where-Object { $_ }
    $before = @{}; foreach ($p in $live) { $before[$p.Id] = $p.TotalProcessorTime.TotalSeconds }
    Start-Sleep -Seconds $Seconds
    $cores = [Environment]::ProcessorCount
    foreach ($p in $live) {
        $p.Refresh()
        $cpu = ($p.TotalProcessorTime.TotalSeconds - $before[$p.Id]) / $Seconds / $cores * 100
        Write-Output ("pid {0}: cpu {1:N1}% of {2} threads, ram {3} MB, vram {4} MB, gpu3d {5}%" -f `
            $p.Id, $cpu, $cores, [Math]::Round($p.WorkingSet64 / 1MB), (Get-GpuMemoryMB $p.Id), (Get-Gpu3dPercent $p.Id))
    }
}

function Write-Status {
    foreach ($item in Read-State) {
        $alive = [bool](Get-Process -Id $item.Pid -ErrorAction SilentlyContinue)
        Write-Output "instance $($item.Index) pid $($item.Pid) running=$alive"
    }
    if (Test-Path $guardFlag) { Write-Output "USER RETURNED: $(Get-Content $guardFlag)" }
}

function Stop-Instances {
    $items = Read-State
    foreach ($item in $items) { Stop-SameProcess $item.Pid $item.Ticks }
    $pids = @($items | ForEach-Object { $_.Pid })
    if ($pids.Count) { Wait-Process -Id $pids -Timeout 15 -ErrorAction SilentlyContinue }
    $first = $items | Select-Object -First 1
    if ($first -and $first.Guard) { Stop-SameProcess $first.Guard $first.GuardTicks }
    Write-Output "stopped $(@($items).Count) instance(s)"
    if (Test-Path $guardFlag) { Write-Output "USER RETURNED: $(Get-Content $guardFlag)"; exit 3 }
}

switch ($Action) {
    'start' { Start-Instances }
    'shot' { Save-InstanceShot $Instance }
    'shots' { foreach ($item in Read-State) { Save-InstanceShot $item.Index } }
    'key' { Send-InstanceKey 'key' }
    'down' { Send-InstanceKey 'down' }
    'up' { Send-InstanceKey 'up' }
    'pad' { Send-InstancePad 'pad' }
    'hold' { Send-InstancePad 'hold' }
    'release' { Send-InstancePad 'release' }
    'stats' { Write-Stats }
    'status' { Write-Status }
    'stop' { Stop-Instances }
}
