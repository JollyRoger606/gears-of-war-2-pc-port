<#
.SYNOPSIS
  Builds the Android APK of the game (arm64-v8a) from your own default.xex.

.DESCRIPTION
  The whole chain, each step skipped when its output is already current:
  0. SDK sources: clones ReXGlue 923c1a5 into out\sdk-android-src and applies every
     sdk\patches\*.patch (0007 is the Android port), like scripts\build-sdk.ps1 does for
     Windows; prepared again whenever a patch changes. -SdkSource uses your own tree instead.
  1. Codegen: when generated\default is missing, when gow2_manifest.toml or game\default.xex
     is newer than generated\default\sources.cmake and -Sdk is given (otherwise it warns and
     builds the existing code), or with -Codegen, runs the Windows SDK's `rexglue codegen`
     on game\default.xex, like scripts\build.ps1.
  2. Android sources: tools\prepare_android_sources.py copies generated\default to
     out\android\generated\default and applies the switch-trap, game and ARM memory-barrier
     patches to the copy (generated\default itself is never patched for ARM).
  3. Configures android\gamelibs with the NDK's CMake toolchain into out\android\build: the
     rexglue-sdk source tree and the game are one build.
  4. SDK: builds libSDL3.so, librexruntime.so and librexgpu-xenos.so.
  5. Game: builds libgow2.so and stages the four libraries plus libc++_shared.so in
     out\android\jniLibs\arm64-v8a.
  6. Checks them with tools\check_android_libs.py (dependencies, symbols, one SDL, SDL_main,
     16 KB pages).
  7. Gradle: android\gradlew.bat assembleDebug -Pgow2.gameLibraries=true, then checks the APK.

  Full logs go to out\android\logs. A first build took 10 minutes on a busy 24-thread PC
  (CMake's first configure about 3, the SDK about 4, the 223 generated files about 3).

.PARAMETER SdkSource
  Developer override: build this rexglue-sdk tree instead of out\sdk-android-src, for example the
  worktree you change the Android port in (ReXGlue 923c1a5 with all of sdk\patches applied). Or
  set REXGLUE_SDK_ANDROID. The build stops when the tree lacks a patch the game needs.

.PARAMETER Sdk
  Windows ReXGlue SDK folder with bin\rexglue.exe; only needed for codegen. Or set REXGLUE_SDK.

.PARAMETER Ndk
  Android NDK folder (r29 or newer). Default: ndk\<ndkVersion of android\app\build.gradle>
  under ANDROID_HOME, ANDROID_SDK_ROOT, sdk.dir of android\local.properties or
  %LOCALAPPDATA%\Android\Sdk; else ANDROID_NDK_HOME; else the newest NDK under those SDK folders.

.PARAMETER Api
  Must equal minSdk in android\app\build.gradle, which is the default; the build stops when it differs.

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
  powershell -ExecutionPolicy Bypass -File scripts\build-android.ps1 -LowPriority

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\build-android.ps1 -SdkSource C:\src\rexglue-sdk-android -LowPriority
#>
param(
    [string]$SdkSource = $env:REXGLUE_SDK_ANDROID,
    [string]$Sdk = $env:REXGLUE_SDK,
    [string]$Ndk = '',
    [int]$Api = 0,
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
. (Join-Path $PSScriptRoot 'sdk-source.ps1')
if ($LowPriority) { (Get-Process -Id $PID).PriorityClass = 'BelowNormal' }

$out = Join-Path $root 'out\android'
$build = Join-Path $out 'build'
$logs = Join-Path $out 'logs'
$genCopy = Join-Path $out 'generated\default'
$jniLibs = Join-Path $out 'jniLibs'
$abiLibs = Join-Path $jniLibs 'arm64-v8a'
$apk = Join-Path $root 'android\app\build\outputs\apk\debug\app-debug.apk'
$cleanSdk = Join-Path $root 'out\sdk-android-src'
$cleanSdkMarker = Join-Path $root 'out\sdk-android-patched.txt'

# Files and texts only a tree with the current patches has; an older Android worktree lacks some.
$SdkTreeMarkers = @(
    @{ Path = 'include\rex\main_android.h'; Text = ''; What = 'the Android port (patch 0007)' },
    @{ Path = 'thirdparty\ffmpeg-android\config.h'; Text = '';
       What = "FFmpeg's Android config outside the FFmpeg submodule (patch 0007)" },
    @{ Path = 'src\filesystem\devices\host_path_device.cpp'; Text = 'GOW2_VFS_OVERRIDES';
       What = 'GOW2_VFS_OVERRIDES (patch 0005)' },
    @{ Path = 'src\input\stick_deadzone.h'; Text = ''; What = 'pad_right_stick_deadzone (patch 0005)' },
    @{ Path = 'src\input\mnk\mnk_input_driver.cpp'; Text = 'mnk_mouse_stick'; What = 'mnk_mouse_stick (patch 0006)' }
)

function Fail($msg) { Write-Host "BUILD FAILED: $msg" -ForegroundColor Red; exit 1 }
function Step($msg) { Write-Host "`n== $msg" -ForegroundColor Cyan }

function Read-GradleValue([string]$name) {
    $gradle = Join-Path $root 'android\app\build.gradle'
    if (-not (Test-Path $gradle)) { return '' }
    $text = Get-Content $gradle -Raw
    $match = [regex]::Match($text, '(?m)^\s*' + [regex]::Escape($name) + '\b\s*=?\s*[''"]?([^''"\s]+)')
    if ($match.Success) { return $match.Groups[1].Value }
    return ''
}

function Get-AndroidSdkRoots {
    $candidates = @($env:ANDROID_HOME, $env:ANDROID_SDK_ROOT)
    $props = Join-Path $root 'android\local.properties'
    if (Test-Path $props) {
        $match = [regex]::Match((Get-Content $props -Raw), '(?m)^\s*sdk\.dir\s*=\s*(.+?)\s*$')
        if ($match.Success) {
            $candidates += $match.Groups[1].Value.Trim().Replace('\:', ':').Replace('\\', '\')
        }
    }
    if ($env:LOCALAPPDATA) { $candidates += (Join-Path $env:LOCALAPPDATA 'Android\Sdk') }
    $roots = @()
    $seen = @{}
    foreach ($candidate in $candidates) {
        if (-not $candidate) { continue }
        if (-not (Test-Path $candidate)) { continue }
        $full = (Resolve-Path $candidate).Path
        $key = $full.ToLowerInvariant()
        if (-not $seen.ContainsKey($key)) {
            $seen[$key] = $true
            $roots += $full
        }
    }
    return $roots
}

function Find-Ndk([string]$pinned) {
    $roots = Get-AndroidSdkRoots
    if ($pinned) {
        foreach ($sdkRoot in $roots) {
            $candidate = Join-Path (Join-Path $sdkRoot 'ndk') $pinned
            if (Test-Path $candidate) { return $candidate }
        }
    }
    if ($env:ANDROID_NDK_HOME -and (Test-Path $env:ANDROID_NDK_HOME)) { return $env:ANDROID_NDK_HOME }
    $best = $null
    $bestVersion = $null
    foreach ($sdkRoot in $roots) {
        $dirs = Get-ChildItem (Join-Path $sdkRoot 'ndk') -Directory -ErrorAction SilentlyContinue
        foreach ($dir in $dirs) {
            try { $ver = [version]($dir.Name -replace '[^0-9.].*$', '') } catch { continue }
            if (($null -eq $bestVersion) -or ($ver -gt $bestVersion)) {
                $bestVersion = $ver
                $best = $dir.FullName
            }
        }
    }
    if ($best) { return $best }
    return $null
}

function Find-AndroidSdk([string]$ndk) {
    foreach ($sdkRoot in (Get-AndroidSdkRoots)) {
        if (Test-Path (Join-Path $sdkRoot 'platforms')) { return $sdkRoot }
    }
    # An NDK installed by the SDK manager lives in <sdk>\ndk\<version>.
    $fromNdk = Split-Path (Split-Path $ndk -Parent) -Parent
    if (Test-Path (Join-Path $fromNdk 'platforms')) { return $fromNdk }
    return $null
}

function Read-NdkRevision([string]$ndk) {
    $revision = ''
    $base = ''
    $props = Join-Path $ndk 'source.properties'
    if (Test-Path $props) {
        $text = Get-Content $props -Raw
        $match = [regex]::Match($text, '(?m)^\s*Pkg\.Revision\s*=\s*(.+?)\s*$')
        if ($match.Success) { $revision = $match.Groups[1].Value.Trim() }
        $matchBase = [regex]::Match($text, '(?m)^\s*Pkg\.BaseRevision\s*=\s*(.+?)\s*$')
        if ($matchBase.Success) { $base = $matchBase.Groups[1].Value.Trim() }
        elseif ($revision) { $base = $revision -replace '-.*$', '' }
    }
    return @{ Revision = $revision; Base = $base }
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

function Test-SdkTreeMarker([string]$tree, [hashtable]$marker) {
    $file = Join-Path $tree $marker.Path
    if (-not (Test-Path $file -PathType Leaf)) { return $false }
    if (-not $marker.Text) { return $true }
    return [bool](Select-String -Path $file -Pattern $marker.Text -SimpleMatch -Quiet)
}

function Get-MissingSdkTreeMarkers([string]$tree, [object[]]$markers) {
    return @($markers | Where-Object { -not (Test-SdkTreeMarker $tree $_) } | ForEach-Object { $_.What })
}

function Assert-SdkTreeCurrent([string]$tree, [string]$remedy) {
    $missing = Get-MissingSdkTreeMarkers $tree $SdkTreeMarkers
    if ($missing.Count -eq 0) { return }
    Write-Host "$tree lacks:" -ForegroundColor Red
    $missing | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    Fail "$tree is not ReXGlue $SdkCommit with the current sdk\patches. $remedy"
}

function Initialize-CleanSdk {
    Step "Preparing the SDK sources in $cleanSdk (ReXGlue $SdkCommit + sdk\patches)"
    try {
        if (-not (Initialize-SdkSource $cleanSdk $cleanSdkMarker (Join-Path $root 'sdk\patches'))) {
            Write-Host '  already prepared with the current patches'
        }
    } catch { Fail $_.Exception.Message }
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

function Get-StaleCodegenInputs([string]$sources) {
    $stale = @()
    $sourcesTime = (Get-Item $sources).LastWriteTimeUtc
    foreach ($name in @('gow2_manifest.toml', 'game\default.xex')) {
        if (-not (Test-Path $name)) { continue }
        if ((Get-Item $name).LastWriteTimeUtc -gt $sourcesTime) { $stale += $name }
    }
    return $stale
}

function Test-CodegenToolsPresent([string]$sdk) {
    if (-not $sdk) { return $false }
    if (-not (Test-Path (Join-Path $sdk 'bin\rexglue.exe'))) { return $false }
    if (-not (Test-Path 'game\default.xex')) { return $false }
    return $true
}

function Invoke-Codegen([string]$reason) {
    Step "Generating C++ from default.xex ($reason)"
    if (-not $Sdk) { Fail "Codegen ($reason) needs the Windows ReXGlue SDK: give -Sdk <folder> (bin\rexglue.exe) or set REXGLUE_SDK." }
    $rexglue = Join-Path $Sdk 'bin\rexglue.exe'
    if (-not (Test-Path $rexglue)) { Fail "rexglue.exe not found in $Sdk\bin." }
    if (-not (Test-Path 'game\default.xex')) {
        Fail 'game\default.xex is missing. Copy default.xex from your own copy of the game into the game folder.'
    }
    Invoke-Step 'codegen' $rexglue @('--log-file', (Join-Path $logs 'codegen-rexglue.log'), 'codegen', 'gow2_manifest.toml')
}

function Read-SdkVersionString {
    $header = Join-Path $build 'rexglue-sdk\include\rex\version.h'
    if (-not (Test-Path $header)) { return '' }
    $line = Select-String -Path $header -Pattern '#define REXGLUE_VERSION_STRING "([^"]+)"' |
        Select-Object -First 1
    if ($line) { return $line.Matches[0].Groups[1].Value }
    return ''
}

function Test-SameCommit([string]$left, [string]$right) {
    $l = $left.ToLowerInvariant()
    $r = $right.ToLowerInvariant()
    return $l.StartsWith($r) -or $r.StartsWith($l)
}

# The version comes from `git describe` in the tree rex_resolve_version runs in; a commit other
# than the SDK tree's HEAD means it described another repository (gow2-recomp).
function Show-SdkVersion([string]$tree) {
    $version = Read-SdkVersionString
    if (-not $version) { return }
    Write-Host "  SDK version: $version"
    if ($version -notmatch '-(dev|rc)\.g([0-9a-f]+)$') { return }
    $described = $Matches[2]
    $head = Get-GitShortHead $tree
    if (-not $head -or (Test-SameCommit $described $head)) { return }
    Write-Host ("  warning: the SDK version $version does not describe $tree (HEAD $head): " +
        'rex_resolve_version ran in another repository (patch 0007 passes SOURCE_DIR to it).') -ForegroundColor Yellow
}

# Windows PowerShell 5.1 turns native stderr into errors, so 'Stop' is lifted while git runs.
function Get-GitShortHead([string]$dir) {
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $out = & git -C $dir rev-parse --short=7 HEAD 2>&1
    $code = $LASTEXITCODE
    $ErrorActionPreference = $previous
    if ($code -ne 0) { return '' }
    return ("$out").Trim()
}

# ---- checks ---------------------------------------------------------------------------------
Step 'Checking the tools'
foreach ($tool in 'git', 'cmake', 'ninja') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { Fail "$tool was not found on PATH." }
}
$python = Find-Python
if (-not $python) { Fail 'Python 3 was not found (python, python3 or py on PATH).' }
if ($SdkSource) {
    if (-not (Test-Path $SdkSource -PathType Container)) { Fail "-SdkSource $SdkSource does not exist." }
    $SdkSource = (Resolve-Path $SdkSource).Path
    Assert-SdkTreeCurrent $SdkSource ('Run without -SdkSource to build from a clean clone, or apply ' +
        'the missing patches to your tree (sdk\patches\README.md).')
}
$minSdk = Read-GradleValue 'minSdk'
if (-not $minSdk) { Fail 'minSdk was not found in android\app\build.gradle.' }
if ($Api -eq 0) { $Api = [int]$minSdk }
elseif ($Api -ne [int]$minSdk) { Fail "-Api $Api differs from minSdk $minSdk in android\app\build.gradle. The libraries must target the app's minSdk; change minSdk there instead." }
$pinnedNdk = Read-GradleValue 'ndkVersion'
if (-not $Ndk) { $Ndk = Find-Ndk $pinnedNdk }
$toolchain = if ($Ndk) { Join-Path $Ndk 'build\cmake\android.toolchain.cmake' } else { '' }
if (-not $Ndk -or -not (Test-Path $toolchain)) { Fail 'Android NDK not found; pass -Ndk <folder> (NDK r29 or newer).' }
$androidSdk = Find-AndroidSdk $Ndk
if (-not $SkipGradle -and -not $androidSdk) { Fail 'Android SDK not found; set ANDROID_HOME (platform 36 is needed).' }
if ($Jobs -le 0) { $Jobs = [Environment]::ProcessorCount }
$rev = Read-NdkRevision $Ndk
Write-Host "SDK source: $(if ($SdkSource) { $SdkSource } else { "$cleanSdk (clean clone)" })"
Write-Host "NDK:        $Ndk ($($rev.Revision))"
if ($pinnedNdk -and ($pinnedNdk -ne $rev.Base)) {
    Write-Host "  warning: android\app\build.gradle pins NDK $pinnedNdk; this NDK is $($rev.Revision)" -ForegroundColor Yellow
}
Write-Host "API:        $Api (minSdk in android\app\build.gradle)"
Write-Host "Python:     $python"
Write-Host "Jobs:       $Jobs"
New-Item -ItemType Directory -Force -Path $logs | Out-Null

if ($Clean) {
    Step 'Cleaning'
    foreach ($d in $build, $jniLibs) { if (Test-Path $d) { Remove-Item $d -Recurse -Force } }
}

# ---- 0. SDK sources --------------------------------------------------------------------------
if (-not $SdkSource) {
    Initialize-CleanSdk
    $SdkSource = $cleanSdk
    Assert-SdkTreeCurrent $SdkSource "Delete $cleanSdkMarker to prepare it again."
}

# ---- 1. codegen -----------------------------------------------------------------------------
$sourcesCmake = 'generated\default\sources.cmake'
if ($Codegen) {
    Invoke-Codegen 'forced with -Codegen'
} elseif (-not (Test-Path $sourcesCmake)) {
    Invoke-Codegen 'generated\default is missing'
} else {
    $stale = @(Get-StaleCodegenInputs $sourcesCmake)
    if ($stale.Count -gt 0) {
        if (Test-CodegenToolsPresent $Sdk) {
            Invoke-Codegen "generated\default is older than $($stale -join ', ')"
        } else {
            Write-Host "  warning: generated\default is older than $($stale -join ', '); give -Sdk <folder> (bin\rexglue.exe) and game\default.xex to regenerate it. Building the existing generated code." -ForegroundColor Yellow
        }
    }
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
Show-SdkVersion $SdkSource

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
