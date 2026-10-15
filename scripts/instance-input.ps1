# Focus one System Link instance, send it keys/clicks and take screenshots of its window.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File scripts/instance-input.ps1 -OutDir out\pcpc-test\run1 -Instance client -Steps "Down,wait:800,Space,wait:3000,shot:after-space"
param(
    [Parameter(Mandatory)] [string]$OutDir,
    [Parameter(Mandatory)] [string]$Instance,
    [Parameter(Mandatory)] [string]$Steps,
    [int]$DefaultHoldMs = 120
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class GowInput {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [DllImport("user32.dll")] public static extern void mouse_event(uint flags, int dx, int dy, uint data, UIntPtr extra);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern int GetWindowThreadProcessId(IntPtr h, out int pid);
  [DllImport("kernel32.dll")] public static extern int GetCurrentThreadId();
  [DllImport("user32.dll")] public static extern bool AttachThreadInput(int a, int b, bool f);
  [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint type);
}
"@

function Get-ElapsedSec([datetime]$start) {
    return [int]((Get-Date) - $start).TotalSeconds
}

function Get-InstanceEntry([string]$path, [string]$name) {
    if (-not (Test-Path -LiteralPath $path)) { throw "instances file not found: $path" }
    # Windows PowerShell 5.1 emits a JSON array as one object; @(...) would nest it.
    $entries = (Get-Content -LiteralPath $path -Raw | ConvertFrom-Json)
    foreach ($e in $entries) { if ($e.name -eq $name) { return $e } }
    throw "instance '$name' not found in $path"
}

function Get-InstanceWindow([System.Diagnostics.Process]$proc, [Int64]$fallbackHwnd) {
    $proc.Refresh()
    if ($proc.MainWindowHandle -ne [IntPtr]::Zero) { return $proc.MainWindowHandle }
    if ($fallbackHwnd -ne 0) { return New-Object IntPtr $fallbackHwnd }
    throw 'instance window handle is unknown (no main window yet)'
}

function Set-InstanceFocus([IntPtr]$hwnd) {
    if ([GowInput]::GetForegroundWindow() -eq $hwnd) { Write-Output 'focus ok'; return $true }
    if ([GowInput]::IsIconic($hwnd)) { [GowInput]::ShowWindow($hwnd, 9) | Out-Null }
    $discard = 0
    $fgThread = [GowInput]::GetWindowThreadProcessId([GowInput]::GetForegroundWindow(), [ref]$discard)
    $me = [GowInput]::GetCurrentThreadId()
    [GowInput]::AttachThreadInput($me, $fgThread, $true) | Out-Null
    [GowInput]::BringWindowToTop($hwnd) | Out-Null
    [GowInput]::SetForegroundWindow($hwnd) | Out-Null
    [GowInput]::AttachThreadInput($me, $fgThread, $false) | Out-Null
    Start-Sleep -Milliseconds 150
    if ([GowInput]::GetForegroundWindow() -ne $hwnd) {
        # Alt tap lets a background process steal focus from another game window.
        [GowInput]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero)
        [GowInput]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
        [GowInput]::SetForegroundWindow($hwnd) | Out-Null
        Start-Sleep -Milliseconds 150
    }
    if ([GowInput]::GetForegroundWindow() -eq $hwnd) { Write-Output 'focus ok'; return $true }
    Write-Output 'focus FAILED'
    return $false
}

function Invoke-FocusStep([IntPtr]$hwnd) {
    $out = @(Set-InstanceFocus $hwnd)
    foreach ($o in $out) { if ($o -is [string]) { Write-Output $o } }
}

function Test-InstanceFocus([IntPtr]$hwnd) {
    $result = @(Set-InstanceFocus $hwnd) | Where-Object { $_ -is [bool] } | Select-Object -Last 1
    return [bool]$result
}

function Send-InstanceKey([IntPtr]$hwnd, [string]$instance, [string]$name, [int]$holdMs, [datetime]$start) {
    # A key sent without focus lands in whatever window has it, such as a Windows Security prompt.
    if (-not (Test-InstanceFocus $hwnd)) { throw "instance '$instance' has no focus; key '$name' not sent" }
    try { $parsed = [Enum]::Parse([System.Windows.Forms.Keys], $name, $true) }
    catch { throw "unknown key '$name'" }
    if ([int]$parsed -gt 255) { throw "key '$name' has no virtual-key code" }
    $vk = [byte]$parsed
    $scan = [byte][GowInput]::MapVirtualKey($vk, 0)
    $label = $parsed.ToString()
    $flags = 0
    if ($label -eq 'Up' -or $label -eq 'Down' -or $label -eq 'Left' -or $label -eq 'Right' -or $label -eq 'Insert' -or $label -eq 'Delete' -or $label -eq 'Home' -or $label -eq 'End' -or $label -eq 'PageUp' -or $label -eq 'PageDown') { $flags = 0x0001 }
    [GowInput]::keybd_event($vk, $scan, $flags, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds $holdMs
    [GowInput]::keybd_event($vk, $scan, ($flags -bor 0x0002), [UIntPtr]::Zero)
    $sec = Get-ElapsedSec $start
    Write-Output "[$instance +${sec}s] key $label (${holdMs}ms)"
}

function Save-InstanceShot([IntPtr]$hwnd, [string]$instance, [string]$label, [string]$shotsDir, [datetime]$start) {
    Set-InstanceFocus $hwnd | Out-Null
    Start-Sleep -Milliseconds 200
    $r = New-Object GowInput+RECT
    [GowInput]::GetWindowRect($hwnd, [ref]$r) | Out-Null
    $w = $r.R - $r.L
    $h = $r.B - $r.T
    if ($w -le 0 -or $h -le 0) { throw "empty window rect for instance '$instance'" }
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
    $stamp = Get-Date -Format 'HHmmss'
    $path = Join-Path $shotsDir "$instance-$stamp-$label.png"
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose()
    $bmp.Dispose()
    $sec = Get-ElapsedSec $start
    Write-Output "[$instance +${sec}s] shot $path (${w}x${h})"
}

function Send-InstanceClick([IntPtr]$hwnd, [string]$instance, [datetime]$start) {
    $r = New-Object GowInput+RECT
    [GowInput]::GetWindowRect($hwnd, [ref]$r) | Out-Null
    $x = $r.L + [int](($r.R - $r.L) / 2)
    $y = $r.T + [int](($r.B - $r.T) / 2)
    [GowInput]::SetCursorPos($x, $y) | Out-Null
    [GowInput]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 80
    [GowInput]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
    $sec = Get-ElapsedSec $start
    Write-Output "[$instance +${sec}s] click $x,$y"
}

$root = Split-Path -Parent $PSScriptRoot
if (-not [System.IO.Path]::IsPathRooted($OutDir)) { $OutDir = Join-Path $root $OutDir }
$OutDir = (Resolve-Path -LiteralPath $OutDir).Path
$shotsDir = Join-Path $OutDir 'shots'
New-Item -ItemType Directory -Force -Path $shotsDir | Out-Null
$start = Get-Date
try {
    $entry = Get-InstanceEntry (Join-Path $OutDir 'instances.json') $Instance
    try { $proc = Get-Process -Id $entry.pid }
    catch { throw "instance '$Instance' process (pid $($entry.pid)) is missing" }
    if ($proc.HasExited) { throw "instance '$Instance' process (pid $($entry.pid)) has exited" }
    $hwnd = Get-InstanceWindow $proc ([Int64]$entry.hwnd)
    $stepList = @($Steps -split ',' | Where-Object { $_ })
    foreach ($raw in $stepList) {
        $proc.Refresh()
        if ($proc.HasExited) { throw "instance '$Instance' process (pid $($entry.pid)) has exited" }
        $token = $raw.Trim()
        if ($token -eq '') { continue }
        $parts = @($token -split ':')
        $head = $parts[0].ToLower()
        if ($head -eq 'wait') {
            if ($parts.Count -ne 2) { throw "bad step '$token' (want wait:<ms>)" }
            $ms = 0
            if (-not [int]::TryParse($parts[1], [ref]$ms)) { throw "bad step '$token' (want wait:<ms>)" }
            Start-Sleep -Milliseconds $ms
            $sec = Get-ElapsedSec $start
            Write-Output "[$Instance +${sec}s] wait ${ms}ms"
        } elseif ($head -eq 'shot') {
            if ($parts.Count -ne 2 -or $parts[1] -eq '') { throw "bad step '$token' (want shot:<label>)" }
            Save-InstanceShot $hwnd $Instance $parts[1] $shotsDir $start
        } elseif ($head -eq 'click') {
            if ($parts.Count -ne 1) { throw "bad step '$token' (want click)" }
            Send-InstanceClick $hwnd $Instance $start
        } elseif ($head -eq 'focus') {
            if ($parts.Count -ne 1) { throw "bad step '$token' (want focus)" }
            Invoke-FocusStep $hwnd
        } elseif ($head -eq 'key') {
            if ($parts.Count -lt 2 -or $parts.Count -gt 3 -or $parts[1] -eq '') { throw "bad step '$token' (want key:<name>[:<holdMs>])" }
            $hold = $DefaultHoldMs
            if ($parts.Count -eq 3) {
                if (-not [int]::TryParse($parts[2], [ref]$hold)) { throw "bad step '$token' (want key:<name>[:<holdMs>])" }
            }
            Send-InstanceKey $hwnd $Instance $parts[1] $hold $start
        } elseif ($parts.Count -eq 1) {
            Send-InstanceKey $hwnd $Instance $parts[0] $DefaultHoldMs $start
        } else {
            throw "unknown step '$token'"
        }
    }
} catch {
    Write-Output $_.Exception.Message
    exit 1
}
