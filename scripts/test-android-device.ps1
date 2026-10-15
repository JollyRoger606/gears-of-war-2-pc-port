<#
.SYNOPSIS
  Builds and runs the runtime self-test (UTF-8 decoder and input lock of patch 0007) on a connected Android phone.

.DESCRIPTION
  Needs out\android\build configured by scripts\build-android.ps1: it builds the target
  gow2_runtime_selftest there, pushes it with libSDL3.so and librexruntime.so from
  out\android\build\sdk and libc++_shared.so from out\android\jniLibs\arm64-v8a to
  /data/local/tmp/gow2-selftest, runs it with LD_LIBRARY_PATH=. and exits with its exit code.
  Nothing is installed; the app and its data are not touched.

.PARAMETER Serial
  Device serial for adb -s. Default: $env:ANDROID_SERIAL (adb's own default when unset).

.PARAMETER SkipBuild
  Skip the cmake build and push the self-test already in out\android\build.

.PARAMETER LowPriority
  Run below normal priority (cmake, ninja and the compiler inherit it).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\test-android-device.ps1 -LowPriority

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\test-android-device.ps1 -Serial emulator-5554 -SkipBuild
#>
param(
    [string]$Serial = $env:ANDROID_SERIAL,
    [switch]$SkipBuild,
    [switch]$LowPriority
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if ($LowPriority) { (Get-Process -Id $PID).PriorityClass = 'BelowNormal' }

function Fail($msg) { Write-Host "SELFTEST FAILED: $msg" -ForegroundColor Red; exit 1 }
function Step($msg) { Write-Host "`n== $msg" -ForegroundColor Cyan }

function Find-Adb {
    $cmd = Get-Command adb -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    foreach ($sdk in @($env:ANDROID_HOME, $env:ANDROID_SDK_ROOT)) {
        if (-not $sdk) { continue }
        $candidate = Join-Path $sdk 'platform-tools\adb.exe'
        if (Test-Path $candidate) { return $candidate }
    }
    if ($env:LOCALAPPDATA) {
        $candidate = Join-Path $env:LOCALAPPDATA 'Android\Sdk\platform-tools\adb.exe'
        if (Test-Path $candidate) { return $candidate }
    }
    return $null
}

function Get-AdbPrefix([string]$serial) {
    if ($serial) { return @('-s', $serial) }
    return @()
}

# Windows PowerShell 5.1 turns native stderr into errors, so 'Stop' is lifted while the
# command runs and the exit code decides. Every output line is echoed for the log.
function Invoke-Native([string]$exe, [string[]]$arguments) {
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & $exe @arguments 2>&1 | ForEach-Object { Write-Host "  $_" }
    $code = $LASTEXITCODE
    $ErrorActionPreference = $previous
    return $code
}

function Build-Selftest {
    if (-not (Test-Path (Join-Path $build 'build.ninja'))) {
        Fail 'out\android\build is not configured; run scripts\build-android.ps1 -SkipGradle first.'
    }
    $code = Invoke-Native 'cmake' @('--build', $build, '--target', 'gow2_runtime_selftest')
    if ($code -ne 0) { Fail "building gow2_runtime_selftest failed (exit code $code)." }
}

function Push-Selftest {
    $files = @(
        (Join-Path $build 'gow2_runtime_selftest'),
        (Join-Path $build 'sdk\libSDL3.so'),
        (Join-Path $build 'sdk\librexruntime.so'),
        (Join-Path $root 'out\android\jniLibs\arm64-v8a\libc++_shared.so')
    )
    foreach ($file in $files) {
        if (-not (Test-Path $file)) { Fail "required file is missing: $file" }
    }
    $remote = '/data/local/tmp/gow2-selftest'
    if ((Invoke-Native $adb ($prefix + @('shell', 'mkdir', '-p', $remote))) -ne 0) {
        Fail "creating $remote on the phone failed."
    }
    foreach ($file in $files) {
        if ((Invoke-Native $adb ($prefix + @('push', $file, $remote))) -ne 0) {
            Fail "pushing $file to the phone failed."
        }
    }
    if ((Invoke-Native $adb ($prefix + @('shell', 'chmod', '755', "$remote/gow2_runtime_selftest"))) -ne 0) {
        Fail 'making the self-test executable on the phone failed.'
    }
}

function Invoke-Selftest {
    # Single-quoted so $? reaches the phone's shell instead of expanding here.
    $command = 'cd /data/local/tmp/gow2-selftest && LD_LIBRARY_PATH=. ./gow2_runtime_selftest; echo SELFTEST_EXIT=$?'
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $lines = & $adb @($prefix + @('shell', $command)) 2>&1
    $ErrorActionPreference = $previous
    foreach ($line in $lines) { Write-Host "  $line" }
    foreach ($line in $lines) {
        if ("$line" -match '^SELFTEST_EXIT=(\d+)') { return [int]$Matches[1] }
    }
    Fail 'the self-test did not report an exit code'
}

$adb = Find-Adb
if (-not $adb) {
    Fail 'adb was not found on PATH or in the usual SDK platform-tools folders; install the Android platform tools or set ANDROID_HOME.'
}
$build = Join-Path $root 'out\android\build'
$prefix = @(Get-AdbPrefix $Serial)
if (-not $SkipBuild) {
    Step 'Building the runtime self-test'
    Build-Selftest
}
Step 'Pushing the self-test to the phone'
Push-Selftest
Step 'Running the self-test on the phone'
$code = Invoke-Selftest
if ($code -eq 0) {
    Write-Host 'Self-test passed' -ForegroundColor Green
    exit 0
} else {
    Write-Host "Self-test failed (exit code $code)" -ForegroundColor Red
    exit $code
}
