<#
.SYNOPSIS
  Builds Gears of War 2 (PC) from your own default.xex.

.DESCRIPTION
  1. Runs `rexglue codegen` on game\default.xex (writes generated\).
  2. Configures CMake (Ninja + clang) and builds gow2.exe.

  Adapted from OverkillLabs3/gears-judgement-recomp (BSD-3-Clause).

.PARAMETER Sdk
  ReXGlue SDK folder (contains bin\rexglue.exe). Or set REXGLUE_SDK.

.PARAMETER Config
  CMake build type: Release (default), RelWithDebInfo or Debug.

.PARAMETER SkipCodegen
  Reuse the existing generated\ sources.

.PARAMETER Clean
  Delete generated\ and out\build first.
#>
param(
    [string]$Sdk = $env:REXGLUE_SDK,
    [ValidateSet('Release', 'RelWithDebInfo', 'Debug')][string]$Config = 'Release',
    [switch]$SkipCodegen,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

function Fail($msg) { Write-Host "ERROR: $msg" -ForegroundColor Red; exit 1 }
function Step($msg) { Write-Host "`n== $msg" -ForegroundColor Cyan }

function Find-VsWithMsvcHeaders {
    # Some VS installs ship cl.exe without the toolset headers (excpt.h); pick one that has them.
    # Outside 64-bit Windows ${env:ProgramFiles(x86)} is missing; return $null so the caller
    # reports the clean "no toolchain" error instead of crashing in Join-Path.
    $pf86 = ${env:ProgramFiles(x86)}
    if (-not $pf86) { return $null }
    $vswhere = Join-Path $pf86 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return $null }
    $paths = & $vswhere -all -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    foreach ($p in $paths) {
        if (Get-ChildItem -Path (Join-Path $p 'VC\Tools\MSVC\*\include\excpt.h') -ErrorAction SilentlyContinue) { return $p }
    }
    return $null
}

function Import-VcVars($vsPath) {
    $vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path $vcvars)) { Fail "vcvars64.bat not found in $vsPath." }
    cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
    }
}

function Find-ClangDir($vsPath) {
    # Either variable can be missing (non-Windows shell, no VS); only probe roots that exist.
    $roots = @()
    if ($vsPath) { $roots += Join-Path $vsPath 'VC\Tools\Llvm\x64\bin' }
    if ($env:ProgramFiles) { $roots += Join-Path $env:ProgramFiles 'LLVM\bin' }
    foreach ($dir in $roots) {
        if (Test-Path (Join-Path $dir 'clang++.exe')) { return $dir }
    }
    $onPath = Get-Command clang++ -ErrorAction SilentlyContinue
    if ($onPath) { return Split-Path $onPath.Source }
    return $null
}

# ---- checks -----------------------------------------------------------------------------
if (-not $Sdk) { Fail 'Give the ReXGlue SDK folder with -Sdk <folder> or set REXGLUE_SDK.' }
$Sdk = (Resolve-Path $Sdk).Path
$rexglue = Join-Path $Sdk 'bin\rexglue.exe'
if (-not (Test-Path $rexglue)) { Fail "rexglue.exe not found in $Sdk\bin." }
if (-not (Test-Path 'game\default.xex')) {
    Fail 'game\default.xex is missing. Copy default.xex from your own copy of the game into the game folder.'
}
foreach ($tool in 'cmake', 'ninja') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { Fail "$tool was not found on PATH." }
}

Step 'Locating the C++ toolchain'
$vsPath = Find-VsWithMsvcHeaders
if (-not $vsPath) { Fail 'No Visual Studio / Build Tools install with a complete MSVC toolset (excpt.h) was found.' }
Import-VcVars $vsPath
$llvm = Find-ClangDir $vsPath
if (-not $llvm) { Fail 'clang++ was not found. Install LLVM or the VS "C++ Clang tools for Windows" component.' }
$env:PATH = "$llvm;$env:PATH"
Write-Host "Visual Studio: $vsPath"
Write-Host "clang:         $llvm"

if ($Clean) {
    Step 'Cleaning'
    foreach ($d in 'generated\default', 'out\build') { if (Test-Path $d) { Remove-Item $d -Recurse -Force } }
}

# ---- codegen --------------------------------------------------------------------------------
if (-not $SkipCodegen) {
    Step 'Generating C++ from default.xex'
    New-Item -ItemType Directory -Force -Path 'out\logs' | Out-Null
    & $rexglue --log-file out\logs\codegen.log codegen gow2_manifest.toml
    if ($LASTEXITCODE -ne 0) { Fail 'rexglue codegen failed (see out\logs\codegen.log).' }
}

# ---- configure + build ------------------------------------------------------------------------
$build = Join-Path $root "out\build\$($Config.ToLower())"
Step "Configuring ($Config)"
cmake -S . -B $build -G Ninja "-DCMAKE_BUILD_TYPE=$Config" `
    "-DCMAKE_C_COMPILER=$llvm\clang.exe" "-DCMAKE_CXX_COMPILER=$llvm\clang++.exe" `
    "-DCMAKE_LINKER=$llvm\lld-link.exe" "-DCMAKE_PREFIX_PATH=$Sdk"
if ($LASTEXITCODE -ne 0) { Fail 'CMake configure failed.' }

Step 'Building'
cmake --build $build --target gow2 gow2_sdk_dlls
if ($LASTEXITCODE -ne 0) {
    # A source patcher can rewrite generated files after ninja planned its compiles; the
    # freshness check then fails the link once and the second build picks the patched tree up.
    Write-Host 'Build reported a problem; trying once more.' -ForegroundColor Yellow
    cmake --build $build --target gow2 gow2_sdk_dlls
    if ($LASTEXITCODE -ne 0) { Fail 'Build failed.' }
}

Write-Host "`nDone: $build\gow2.exe" -ForegroundColor Green
