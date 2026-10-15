<#
.SYNOPSIS
  Builds the Android APK of the game (arm64-v8a) from your own default.xex.

.DESCRIPTION
  The whole chain, each step skipped when its output is already current:
  1. Codegen: when generated\default is missing (or with -Codegen), runs the Windows SDK's
     `rexglue codegen` on game\default.xex, like scripts\build.ps1.
  2. Android sources: tools\prepare_android_sources.py copies generated\default to
     out\android\generated\default and applies the switch-trap, game and ARM memory-barrier
     patches to the copy (generated\default itself is never patched for ARM).
  3. Configures android\gamelibs with the NDK's CMake toolchain into out\android\build: the
     rexglue-sdk source tree (-SdkSource) and the game are one build.
  4. SDK: builds libSDL3.so, librexruntime.so and librexgpu-xenos.so.
  5. Game: builds libgow2.so and stages the four libraries plus libc++_shared.so in
     out\android\jniLibs\arm64-v8a.
  6. Checks them with tools\check_android_libs.py (dependencies, symbols, one SDL, SDL_main,
     16 KB pages).
  7. Gradle: android\gradlew.bat assembleDebug -Pgow2.gameLibraries=true, then checks the APK.

  Full logs go to out\android\logs. A first build took 10 minutes on a busy 24-thread PC
  (CMake's first configure about 3, the SDK about 4, the 223 generated files about 3).

.PARAMETER SdkSource
  rexglue-sdk source tree with the Android port (for example a worktree of ReXGlue 923c1a5 with
  sdk\patches applied and the Android changes on top). Or set REXGLUE_SDK_ANDROID.

.PARAMETER Sdk
  Windows ReXGlue SDK folder with bin\rexglue.exe; only needed for codegen. Or set REXGLUE_SDK.

.PARAMETER Ndk
  Android NDK folder (r29 or newer). Default: ANDROID_NDK_HOME, else the newest NDK under
  %LOCALAPPDATA%\Android\Sdk\ndk.

.PARAMETER Api
  minSdk / ANDROID_PLATFORM level. Default 29, the app's minSdk.

.PARAMETER Jobs
  Parallel compile jobs. Default: the number of logical processors.

.PARAMETER Codegen
  Run codegen even when generated\default exists.

.PARAMETER Reconfigure
  Run the CMake configure step even when the build tree is already configured the same way.

.PARAMETER SkipGradle
  Stop after the native libraries are built and checked.

.PARAMETER Clean
  Delete out\android\build and out\android\jniLibs first.

.PARAMETER LowPriority
  Run below normal priority (cmake, ninja, the compilers and Gradle inherit it).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\build-android.ps1 -SdkSource C:\src\rexglue-sdk-android -LowPriority
#>
param(
    [string]$SdkSource = $env:REXGLUE_SDK_ANDROID,
    [string]$Sdk = $env:REXGLUE_SDK,
    [string]$Ndk = '',
    [int]$Api = 29,
    [int]$Jobs = 0,
    [switch]$Codegen,
    [switch]$Reconfigure,
    [switch]$SkipGradle,
    [switch]$Clean,
    [switch]$LowPriority
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
. (Join-Path $PSScriptRoot 'sdk-symlinks.ps1')
if ($LowPriority) { (Get-Process -Id $PID).PriorityClass = 'BelowNormal' }

$out = Join-Path $root 'out\android'
$build = Join-Path $out 'build'
$logs = Join-Path $out 'logs'
$genCopy = Join-Path $out 'generated\default'
$jniLibs = Join-Path $out 'jniLibs'
$abiLibs = Join-Path $jniLibs 'arm64-v8a'
$apk = Join-Path $root 'android\app\build\outputs\apk\debug\app-debug.apk'

function Fail($msg) { Write-Host "BUILD FAILED: $msg" -ForegroundColor Red; exit 1 }
function Step($msg) { Write-Host "`n== $msg" -ForegroundColor Cyan }

function Find-Ndk {
    if ($env:ANDROID_NDK_HOME -and (Test-Path $env:ANDROID_NDK_HOME)) { return $env:ANDROID_NDK_HOME }
    $base = Join-Path $env:LOCALAPPDATA 'Android\Sdk\ndk'
    $latest = Get-ChildItem $base -Directory -ErrorAction SilentlyContinue |
        Sort-Object { [version]($_.Name -replace '[^0-9.].*$', '') } -Descending | Select-Object -First 1
    if ($latest) { return $latest.FullName }
    return $null
}

function Find-AndroidSdk([string]$ndk) {
    foreach ($candidate in @($env:ANDROID_HOME, $env:ANDROID_SDK_ROOT, (Join-Path $env:LOCALAPPDATA 'Android\Sdk'))) {
        if ($candidate -and (Test-Path (Join-Path $candidate 'platforms'))) { return $candidate }
    }
    # An NDK installed by the SDK manager lives in <sdk>\ndk\<version>.
    $fromNdk = Split-Path (Split-Path $ndk -Parent) -Parent
    if (Test-Path (Join-Path $fromNdk 'platforms')) { return $fromNdk }
    return $null
}

function Find-Python {
    foreach ($name in 'python', 'python3') {
        $cmd = Get-Command $name -ErrorAction SilentlyContinue
        if ($cmd -and $cmd.Source -notmatch 'WindowsApps') { return $cmd.Source }
    }
    $py = Get-Command py -ErrorAction SilentlyContinue
    if ($py) { return (& $py.Source -3 -c 'import sys; print(sys.executable)') }
    return $null
}

function Show-Errors([string]$log) {
    Select-String -Path $log -Pattern 'error:|CMake Error|FAILED:|undefined symbol|ninja: build stopped|BUILD FAILED|^ERROR ' -Context 0, 3 |
        Select-Object -First 60 | ForEach-Object { Write-Host $_ }
}

# Runs a native command with its output in $log, ninja's [n/total] lines as a progress bar, and
# returns its exit code. Windows PowerShell 5.1 turns native stderr into errors, so 'Stop' is
# lifted while it runs and the exit code decides.
function Invoke-Logged([string]$log, [string]$activity, [string]$exe, [string[]]$arguments) {
    "> $exe $($arguments -join ' ')" | Out-File -FilePath $log -Encoding utf8
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & $exe @arguments 2>&1 | ForEach-Object {
        $line = "$_"
        $line | Out-File -FilePath $log -Append -Encoding utf8
        if ($line -match '^\[(\d+)/(\d+)\]') {
            Write-Progress -Activity $activity -Status $line.Substring(0, [Math]::Min(100, $line.Length)) `
                -PercentComplete ([int](100 * [int]$Matches[1] / [int]$Matches[2]))
        }
    }
    $code = $LASTEXITCODE
    $ErrorActionPreference = $previous
    Write-Progress -Activity $activity -Completed
    return $code
}

function Invoke-Step([string]$name, [string]$exe, [string[]]$arguments) {
    $log = Join-Path $logs "$name.log"
    $code = Invoke-Logged $log $name $exe $arguments
    if ($code -ne 0) {
        Show-Errors $log
        Fail "$name failed (exit code $code); full log: $log"
    }
    Write-Host "  ok ($log)"
}

function Read-CacheValue([string]$cache, [string]$name) {
    $line = Select-String -Path $cache -Pattern "^$([regex]::Escape($name)):[A-Z]+=(.*)$" -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($line) { return $line.Matches[0].Groups[1].Value }
    return $null
}

function Test-ConfiguredAs([hashtable]$wanted) {
    $cache = Join-Path $build 'CMakeCache.txt'
    if (-not (Test-Path $cache) -or -not (Test-Path (Join-Path $build 'build.ninja'))) { return $false }
    foreach ($name in $wanted.Keys) {
        $have = Read-CacheValue $cache $name
        if (-not $have -or ($have -replace '\\', '/') -ne ($wanted[$name] -replace '\\', '/')) { return $false }
    }
    return $true
}

function Write-LocalProperties([string]$androidSdk) {
    $props = Join-Path $root 'android\local.properties'
    if (Test-Path $props) { return }
    "sdk.dir=$($androidSdk -replace '\\', '/')" | Out-File -FilePath $props -Encoding ascii
    Write-Host "  wrote android\local.properties (sdk.dir=$androidSdk)"
}

# ---- checks ---------------------------------------------------------------------------------
Step 'Checking the tools'
foreach ($tool in 'cmake', 'ninja') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { Fail "$tool was not found on PATH." }
}
$python = Find-Python
if (-not $python) { Fail 'Python 3 was not found (python, python3 or py on PATH).' }
if (-not $SdkSource) { Fail 'Give the rexglue-sdk tree with the Android port with -SdkSource <folder> or set REXGLUE_SDK_ANDROID.' }
if (-not (Test-Path (Join-Path $SdkSource 'include\rex\main_android.h'))) {
    Fail "$SdkSource is not a rexglue-sdk tree with the Android port (include\rex\main_android.h is missing)."
}
$SdkSource = (Resolve-Path $SdkSource).Path
if (-not $Ndk) { $Ndk = Find-Ndk }
$toolchain = if ($Ndk) { Join-Path $Ndk 'build\cmake\android.toolchain.cmake' } else { '' }
if (-not $Ndk -or -not (Test-Path $toolchain)) { Fail 'Android NDK not found; pass -Ndk <folder> (NDK r29 or newer).' }
$androidSdk = Find-AndroidSdk $Ndk
if (-not $SkipGradle -and -not $androidSdk) { Fail 'Android SDK not found; set ANDROID_HOME (platform 36 is needed).' }
if ($Jobs -le 0) { $Jobs = [Environment]::ProcessorCount }
Write-Host "SDK source: $SdkSource"
Write-Host "NDK:        $Ndk"
Write-Host "Python:     $python"
Write-Host "Jobs:       $Jobs"
New-Item -ItemType Directory -Force -Path $logs | Out-Null

if ($Clean) {
    Step 'Cleaning'
    foreach ($d in $build, $jniLibs) { if (Test-Path $d) { Remove-Item $d -Recurse -Force } }
}

# ---- 1. codegen -----------------------------------------------------------------------------
if ($Codegen -or -not (Test-Path 'generated\default\sources.cmake')) {
    Step 'Generating C++ from default.xex'
    if (-not $Sdk) { Fail 'generated\default is missing: give the Windows ReXGlue SDK with -Sdk <folder> (bin\rexglue.exe) or set REXGLUE_SDK.' }
    $rexglue = Join-Path $Sdk 'bin\rexglue.exe'
    if (-not (Test-Path $rexglue)) { Fail "rexglue.exe not found in $Sdk\bin." }
    if (-not (Test-Path 'game\default.xex')) {
        Fail 'game\default.xex is missing. Copy default.xex from your own copy of the game into the game folder.'
    }
    Invoke-Step 'codegen' $rexglue @('--log-file', (Join-Path $logs 'codegen-rexglue.log'), 'codegen', 'gow2_manifest.toml')
}

# ---- 2. Android copy of the generated code ---------------------------------------------------
Step 'Preparing the Android copy of the generated code'
Invoke-Step 'prepare-sources' $python @((Join-Path $root 'tools\prepare_android_sources.py'),
    '--source', (Join-Path $root 'generated\default'), '--dest', $genCopy)
Get-Content (Join-Path $logs 'prepare-sources.log') | Select-Object -Last 1 | ForEach-Object { Write-Host "  $_" }

# ---- 3. configure ----------------------------------------------------------------------------
Repair-SymlinkStubs $SdkSource
$cacheWanted = @{
    'CMAKE_TOOLCHAIN_FILE' = $toolchain
    'ANDROID_PLATFORM'     = "android-$Api"
    'GOW2_REXSDK_DIR'      = $SdkSource
    'GOW2_GENERATED_DIR'   = $genCopy
    'GOW2_JNILIBS_DIR'     = $jniLibs
}
if ($Reconfigure -or -not (Test-ConfiguredAs $cacheWanted)) {
    Step 'Configuring android\gamelibs (the first time takes several minutes)'
    $cache = Join-Path $build 'CMakeCache.txt'
    # A tree configured from another SDK or NDK keeps stale cached paths; start it over.
    if ((Test-Path $cache) -and -not $Reconfigure) { Remove-Item $cache -Force }
    $configure = @('-S', (Join-Path $root 'android\gamelibs'), '-B', $build, '-G', 'Ninja',
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain", '-DANDROID_ABI=arm64-v8a', "-DANDROID_PLATFORM=android-$Api",
        '-DANDROID_STL=c++_shared', '-DCMAKE_BUILD_TYPE=Release',
        "-DGOW2_REXSDK_DIR=$SdkSource", "-DGOW2_GENERATED_DIR=$genCopy", "-DGOW2_JNILIBS_DIR=$jniLibs")
    Invoke-Step 'configure' 'cmake' $configure
} else {
    Write-Host "`n== Build tree already configured: $build" -ForegroundColor Cyan
}

# ---- 4. SDK ----------------------------------------------------------------------------------
Step 'Building the SDK (libSDL3.so, librexruntime.so, librexgpu-xenos.so)'
Invoke-Step 'build-sdk' 'cmake' @('--build', $build, '--target', 'SDL3-shared', 'rexruntime', 'rexgpu-xenos',
    '--', '-k', '0', '-j', "$Jobs")

# ---- 5. game ---------------------------------------------------------------------------------
Step 'Building the game (libgow2.so) and staging the libraries'
Invoke-Step 'build-game' 'cmake' @('--build', $build, '--target', 'gow2_jnilibs', '--', '-k', '0', '-j', "$Jobs")

# ---- 6. checks -------------------------------------------------------------------------------
Step "Checking the libraries in $abiLibs"
Invoke-Step 'check-libs' $python @((Join-Path $root 'tools\check_android_libs.py'), $abiLibs,
    '--ndk', $Ndk, '--api', "$Api")
Get-Content (Join-Path $logs 'check-libs.log') | Select-Object -Last 1 | ForEach-Object { Write-Host "  $_" }

if ($SkipGradle) {
    Write-Host "`nDone: native libraries in $abiLibs" -ForegroundColor Green
    exit 0
}

# ---- 7. Gradle -------------------------------------------------------------------------------
Step 'Building the APK (Gradle)'
Write-LocalProperties $androidSdk
Invoke-Step 'gradle' (Join-Path $root 'android\gradlew.bat') @('-p', (Join-Path $root 'android'),
    'assembleDebug', '-Pgow2.gameLibraries=true', "-Pgow2.gameLibs.dir=$jniLibs", '--console=plain')
if (-not (Test-Path $apk)) { Fail "Gradle reported success but $apk is missing." }

Step 'Checking the APK'
Invoke-Step 'check-apk' $python @((Join-Path $root 'tools\check_android_libs.py'), $apk, '--ndk', $Ndk, '--api', "$Api")
Get-Content (Join-Path $logs 'check-apk.log') | Select-Object -Last 1 | ForEach-Object { Write-Host "  $_" }

Write-Host "`nDone: $apk" -ForegroundColor Green
Write-Host 'Install with: adb install -r android\app\build\outputs\apk\debug\app-debug.apk'
