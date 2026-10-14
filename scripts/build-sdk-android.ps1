<#
.SYNOPSIS
  Builds the ReXGlue runtime for Android arm64 (bring-up of the Android port, docs/ANDROID.md).

.DESCRIPTION
  Configures an rexglue-sdk source tree with the Android NDK's CMake toolchain (arm64-v8a,
  armv8.2-a, -fno-emulated-tls) into <Source>\out\android-arm64 and builds rexruntime and the GPU
  plugin with the Vulkan backend. Prints only the errors; exit code 1 when the build fails.

.PARAMETER Source
  The rexglue-sdk tree to build (an Android worktree with the patches applied).

.PARAMETER Ndk
  Android NDK folder. Default: the newest one under %LOCALAPPDATA%\Android\Sdk\ndk.

.PARAMETER Api
  minSdk / ANDROID_PLATFORM level. Default 29 (the lowest with ASharedMemory and memfd).

.PARAMETER Targets
  CMake targets to build. Default: rexruntime and rexgpu-xenos.

.PARAMETER Clean
  Delete the Android build folder first.

.PARAMETER LowPriority
  Run the build below normal priority (cmake, ninja and the compilers inherit it), so the PC stays
  responsive while it compiles.
#>
param(
    [Parameter(Mandatory)][string]$Source,
    [string]$Ndk = '',
    [int]$Api = 29,
    [string[]]$Targets = @('rexruntime', 'rexgpu-xenos'),
    [switch]$Clean,
    [switch]$LowPriority
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'sdk-symlinks.ps1')
if ($LowPriority) { (Get-Process -Id $PID).PriorityClass = 'BelowNormal' }

function Fail($msg) { Write-Host "BUILD FAILED: $msg"; exit 1 }

function Find-Ndk {
    $base = Join-Path $env:LOCALAPPDATA 'Android\Sdk\ndk'
    $latest = Get-ChildItem $base -Directory -ErrorAction SilentlyContinue |
        Sort-Object { [version]($_.Name -replace '[^0-9.].*$', '') } -Descending | Select-Object -First 1
    if ($latest) { return $latest.FullName }
    return $null
}

function Show-Errors([string[]]$log) {
    $log | Select-String -Pattern 'error:|CMake Error|FAILED:|undefined symbol|ninja: build stopped' -Context 0, 3 |
        Select-Object -First 60 | ForEach-Object { Write-Host $_ }
}

$Source = (Resolve-Path $Source).Path
if (-not $Ndk) { $Ndk = Find-Ndk }
if (-not $Ndk -or -not (Test-Path (Join-Path $Ndk 'build\cmake\android.toolchain.cmake'))) {
    Fail 'Android NDK not found; pass -Ndk <folder> (NDK r29 or newer).'
}
$build = Join-Path $Source 'out\android-arm64'
if ($Clean -and (Test-Path $build)) { Remove-Item $build -Recurse -Force }

Repair-SymlinkStubs $Source
Write-Host "NDK:    $Ndk"
Write-Host "Source: $Source"

$flags = '-march=armv8.2-a -fno-emulated-tls'
# Windows PowerShell 5.1 stops on the first line CMake writes to stderr (a warning) under 'Stop';
# the exit codes below decide failure instead.
$ErrorActionPreference = 'Continue'
$log = cmake -S $Source -B $build -G Ninja `
    "-DCMAKE_TOOLCHAIN_FILE=$Ndk\build\cmake\android.toolchain.cmake" `
    '-DANDROID_ABI=arm64-v8a' "-DANDROID_PLATFORM=android-$Api" '-DANDROID_STL=c++_shared' `
    '-DCMAKE_BUILD_TYPE=Release' "-DCMAKE_C_FLAGS=$flags" "-DCMAKE_CXX_FLAGS=$flags" `
    '-DREXGLUE_USE_VULKAN=ON' '-DREXGLUE_ENABLE_TRACY=OFF' 2>&1 | ForEach-Object { "$_" }
if ($LASTEXITCODE -ne 0) { Show-Errors $log; Fail 'CMake configure failed.' }

$log = cmake --build $build --target @Targets -- -k 0 2>&1 | ForEach-Object { "$_" }
$code = $LASTEXITCODE
Show-Errors $log
if ($code -ne 0) { Fail "the build failed (exit code $code)." }
Write-Host "BUILD OK: $($Targets -join ', ') for Android arm64 in $build"
