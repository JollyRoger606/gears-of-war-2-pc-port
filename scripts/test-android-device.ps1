<#
.SYNOPSIS
  Builds and runs the on-device tests on a connected Android phone: the runtime self-test (UTF-8
  decoder and input lock of patch 0007) and the touch-controls model test.

.DESCRIPTION
  Needs out\android\build configured by scripts\build-android.ps1: it builds the targets
  gow2_runtime_selftest and gow2_touch_model_test (android/gamelibs/tests/touch_model_test.cpp)
  there, pushes them with libSDL3.so and librexruntime.so from out\android\build\sdk and
  libc++_shared.so from out\android\jniLibs\arm64-v8a to /data/local/tmp/gow2-selftest, runs
  each with LD_LIBRARY_PATH=. and exits with 0 when both pass, otherwise with the first failing
  exit code. Nothing is installed; the app and its data are not touched.

.PARAMETER Serial
  Device serial for adb -s. Default: $env:ANDROID_SERIAL (adb's own default when unset).

.PARAMETER SkipBuild
  Skip the cmake build and push the tests already in out\android\build.

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
    $code = Invoke-Native 'cmake' (@('--build', $build, '--target') + $tests)
    if ($code -ne 0) { Fail "building $($tests -join ', ') failed (exit code $code)." }
}

function Push-Executable([string]$name) {
    $file = Join-Path $build $name
    if (-not (Test-Path $file)) { Fail "required file is missing: $file" }
    if ((Invoke-Native $adb ($prefix + @('push', $file, $remote))) -ne 0) {
        Fail "pushing $file to the phone failed."
    }
    if ((Invoke-Native $adb ($prefix + @('shell', 'chmod', '755', "$remote/$name"))) -ne 0) {
        Fail "making $name executable on the phone failed."
    }
}

function Push-Selftest {
    $files = @(
        (Join-Path $build 'sdk\libSDL3.so'),
        (Join-Path $build 'sdk\librexruntime.so'),
        (Join-Path $root 'out\android\jniLibs\arm64-v8a\libc++_shared.so')
    )
    foreach ($file in $files) {
        if (-not (Test-Path $file)) { Fail "required file is missing: $file" }
    }
    if ((Invoke-Native $adb ($prefix + @('shell', 'mkdir', '-p', $remote))) -ne 0) {
        Fail "creating $remote on the phone failed."
    }
    foreach ($file in $files) {
        if ((Invoke-Native $adb ($prefix + @('push', $file, $remote))) -ne 0) {
            Fail "pushing $file to the phone failed."
        }
    }
    foreach ($name in $tests) { Push-Executable $name }
}

function Invoke-Selftest([string]$name) {
    # The single-quoted half keeps $? for the phone's shell instead of expanding it here.
    $command = "cd $remote && LD_LIBRARY_PATH=. ./$name" + '; echo SELFTEST_EXIT=$?'
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $lines = & $adb @($prefix + @('shell', $command)) 2>&1
    $ErrorActionPreference = $previous
    foreach ($line in $lines) { Write-Host "  $line" }
    foreach ($line in $lines) {
        if ("$line" -match '^SELFTEST_EXIT=(\d+)') { return [int]$Matches[1] }
    }
    Fail "$name did not report an exit code"
}

$adb = Find-Adb
if (-not $adb) {
    Fail 'adb was not found on PATH or in the usual SDK platform-tools folders; install the Android platform tools or set ANDROID_HOME.'
}
$build = Join-Path $root 'out\android\build'
$remote = '/data/local/tmp/gow2-selftest'
$tests = @('gow2_runtime_selftest', 'gow2_touch_model_test')
$prefix = @(Get-AdbPrefix $Serial)
if (-not $SkipBuild) {
    Step 'Building the on-device tests'
    Build-Selftest
}
Step 'Pushing the tests to the phone'
Push-Selftest
$firstFailure = 0
foreach ($name in $tests) {
    Step "Running $name on the phone"
    $code = Invoke-Selftest $name
    if ($code -eq 0) {
        Write-Host "$name passed" -ForegroundColor Green
    } else {
        Write-Host "$name failed (exit code $code)" -ForegroundColor Red
        if ($firstFailure -eq 0) { $firstFailure = $code }
    }
}
exit $firstFailure
