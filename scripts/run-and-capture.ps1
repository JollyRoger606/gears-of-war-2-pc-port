# Boot gow2.exe, take screenshots of its window at the given seconds, then stop it.
# Usage: pwsh -File scripts/run-and-capture.ps1 [-Seconds "10,25,45"] [-OutDir out/shots]
param(
    [string]$Seconds = '10,25,45',
    [string]$OutDir = 'out/shots',
    [string]$GameRoot = 'C:\personal\gow2-gamedata\retail'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class Win {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
}
"@

function Save-WindowShot([System.Diagnostics.Process]$proc, [string]$path) {
    $proc.Refresh()
    $h = $proc.MainWindowHandle
    if ($h -eq [IntPtr]::Zero) { Write-Output "no window yet for $path"; return }
    [Win]::SetForegroundWindow($h) | Out-Null
    $r = New-Object Win+RECT
    [Win]::GetWindowRect($h, [ref]$r) | Out-Null
    $w = $r.R - $r.L; $hgt = $r.B - $r.T
    if ($w -le 0 -or $hgt -le 0) { Write-Output "empty window rect for $path"; return }
    $bmp = New-Object System.Drawing.Bitmap $w, $hgt
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    Write-Output "saved $path ($w x $hgt)"
}

New-Item -ItemType Directory -Force -Path $OutDir, 'out/runs' | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$log = Join-Path $root "out/runs/capture-$stamp.log"
$exe = Join-Path $root 'out/build/release/gow2.exe'
$proc = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -PassThru `
    -ArgumentList "--game_data_root=`"$GameRoot`"", "--log_file=`"$log`""
$start = Get-Date
# pwsh -File passes "8,20,40" as one string, so split it here.
$marks = $Seconds -split ',' | ForEach-Object { [int]$_ } | Sort-Object
foreach ($s in $marks) {
    while (((Get-Date) - $start).TotalSeconds -lt $s) {
        if ($proc.HasExited) { break }
        Start-Sleep -Milliseconds 250
    }
    if ($proc.HasExited) { Write-Output "process exited with code $($proc.ExitCode) before ${s}s"; break }
    Save-WindowShot $proc (Join-Path $OutDir "shot-$stamp-$($s)s.png")
}
if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
Write-Output "log: $log"
