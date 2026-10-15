<#
.SYNOPSIS
  Builds the Android APK of the game (arm64-v8a) from your own default.xex.

.DESCRIPTION
  The whole chain, each step skipped when its output is already current:
  0. SDK sources: clones ReXGlue 923c1a5 into out\sdk-android-src and applies every
     sdk\patches\*.patch (0007 is the Android port), like scripts\build-sdk.ps1 does for
     Windows; prepared again whenever a patch changes. -SdkSource uses your own tree instead.
     Either tree is then compared with 923c1a5 + sdk\patches (through temporary git indexes):
     changes beyond the series are listed and allowed, a missing or changed line of the series
     stops the build.
  1. Codegen: with -Sdk and game\default.xex, the Windows SDK's `rexglue codegen` runs on every
     build (it skips the work when generated\default is current); also when generated\default
     is missing, or with -Codegen. Without them generated\default\codegen.d decides: inputs
     read in another worktree must equal this tree's (the build stops otherwise), and inputs
     newer than codegen.build.stamp only warn.
  2. Android sources: tools\prepare_android_sources.py copies generated\default to
     out\android\generated\default and applies the switch-trap, game and ARM memory-barrier
     patches to the copy (generated\default itself is never patched for ARM).
  3. Configures android\gamelibs with the NDK's CMake toolchain into out\android\build: the
     rexglue-sdk source tree and the game are one build.
  4. SDK: builds libSDL3.so, librexruntime.so and librexgpu-xenos.so.
  5. Game: builds libgow2.so and stages the four libraries plus libc++_shared.so in
     out\android\jniLibs\arm64-v8a.
  6. Checks them with tools\check_android_libs.py (dependencies, symbols, one SDL, SDL_main,
     16 KB pages, no absolute build paths).
  7. Gradle: deletes the old APK, runs android\gradlew.bat assembleDebug
     -Pgow2.gameLibraries=true, then checks the APK.

  Full logs go to out\android\logs. A first build took 10 minutes on a busy 24-thread PC
  (CMake's first configure about 3, the SDK about 4, the 223 generated files about 3).

.PARAMETER SdkSource
  Developer override: build this rexglue-sdk tree instead of out\sdk-android-src, for example the
  worktree you change the Android port in (ReXGlue 923c1a5 with all of sdk\patches applied). Or
  set REXGLUE_SDK_ANDROID. The build stops when the tree lacks a line of sdk\patches or changes
  one; regenerate 0007 from a tree that is newer than the series first.

.PARAMETER Sdk
  Windows ReXGlue SDK folder with bin\rexglue.exe; only needed for codegen, which then runs on
  every build. Or set REXGLUE_SDK.

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

$codegenStamp = 'generated\default\codegen.build.stamp'
$codegenDeps = 'generated\default\codegen.d'
# rexglue codegen rewrites gow2_manifest.toml a few seconds after it writes codegen.build.stamp.
$ManifestRewriteSeconds = 120
$MaxListedPaths = 40

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

function Show-PathList([object[]]$paths, [string]$color) {
    $paths | Select-Object -First $MaxListedPaths | ForEach-Object { Write-Host "  - $_" -ForegroundColor $color }
    if ($paths.Count -gt $MaxListedPaths) {
        Write-Host "  ... and $($paths.Count - $MaxListedPaths) more" -ForegroundColor $color
    }
}

function New-TempPath([string]$suffix) {
    return Join-Path ([IO.Path]::GetTempPath()) ('gow2-' + [guid]::NewGuid().ToString('N') + $suffix)
}

# Windows PowerShell 5.1 turns native stderr into errors, so 'Stop' is lifted while git runs.
function Invoke-TreeGit([string]$tree, [string[]]$arguments) {
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $out = & git -C $tree @arguments 2>$null
    $code = $LASTEXITCODE
    $ErrorActionPreference = $previous
    return @{ Code = $code; Out = @($out | ForEach-Object { "$_".Trim() } | Where-Object { $_ }) }
}

function Restore-GitIndexVariable([string]$previous) {
    if ($previous) { $env:GIT_INDEX_FILE = $previous }
    else { Remove-Item Env:\GIT_INDEX_FILE -ErrorAction SilentlyContinue }
}

function Assert-SdkGitTree([string]$tree) {
    $isRepo = (Invoke-TreeGit $tree @('rev-parse', '--git-dir')).Code -eq 0
    $hasCommit = $isRepo -and ((Invoke-TreeGit $tree @('cat-file', '-e', "$SdkCommit^{commit}")).Code -eq 0)
    if (-not $hasCommit) { Fail "$tree is not a git checkout of rexglue-sdk that has commit $SdkCommit." }
}

function Read-WrittenTree([string]$tree) {
    $written = Invoke-TreeGit $tree @('write-tree')
    if ($written.Code -ne 0) { Fail "git write-tree failed in $tree." }
    return $written.Out[0]
}

function Get-ExpectedSdkTree([string]$tree, [string]$index) {
    $env:GIT_INDEX_FILE = $index
    if ((Invoke-TreeGit $tree @('read-tree', $SdkCommit)).Code -ne 0) { Fail "git read-tree $SdkCommit failed in $tree." }
    foreach ($patch in (Get-SdkPatchFiles (Join-Path $root 'sdk\patches'))) {
        $applied = Invoke-TreeGit $tree @('apply', '--cached', '--ignore-whitespace', '--whitespace=nowarn', $patch.FullName)
        if ($applied.Code -ne 0) { Fail "sdk\patches\$($patch.Name) does not apply to ReXGlue $SdkCommit." }
    }
    return Read-WrittenTree $tree
}

function Get-ActualSdkTree([string]$tree, [string]$index) {
    $env:GIT_INDEX_FILE = $index
    if ((Invoke-TreeGit $tree @('read-tree', 'HEAD')).Code -ne 0) { Fail "git read-tree HEAD failed in $tree." }
    if ((Invoke-TreeGit $tree @('add', '-A')).Code -ne 0) { Fail "git add -A (into a temporary index) failed in $tree." }
    return Read-WrittenTree $tree
}

function Get-TreeDiffPaths([string]$tree, [string]$from, [string]$to) {
    return (Invoke-TreeGit $tree @('diff', '--name-only', $from, $to)).Out
}

# The series' own diff applies in reverse only when every line it adds is in the tree.
function Test-SeriesContainedIn([string]$tree, [string]$expected, [string]$actualIndex) {
    $patch = New-TempPath '.patch'
    try {
        $diff = Invoke-TreeGit $tree @('diff', '--binary', "--output=$patch", $SdkCommit, $expected)
        if ($diff.Code -ne 0) { return $false }
        $env:GIT_INDEX_FILE = $actualIndex
        $check = Invoke-TreeGit $tree @('apply', '--cached', '--check', '-R', '--ignore-whitespace', '--whitespace=nowarn', $patch)
        return $check.Code -eq 0
    } finally {
        Remove-Item $patch -Force -ErrorAction SilentlyContinue
    }
}

function Select-SeriesPaths([string]$tree, [string]$expected, [object[]]$differing) {
    $series = @(Get-TreeDiffPaths $tree $SdkCommit $expected)
    $touched = @($differing | Where-Object { $series -contains $_ })
    if ($touched.Count -gt 0) { return $touched }
    return $differing
}

# Compares the tree's files with ReXGlue $SdkCommit plus every sdk\patches file, through temporary
# indexes so the tree's own index and commits stay as they are.
function Assert-SdkTreeMatchesSeries([string]$tree, [string]$remedy) {
    Assert-SdkGitTree $tree
    $previousIndex = $env:GIT_INDEX_FILE
    $expectedIndex = New-TempPath '.index'
    $actualIndex = New-TempPath '.index'
    try {
        $expected = Get-ExpectedSdkTree $tree $expectedIndex
        $actual = Get-ActualSdkTree $tree $actualIndex
        if ($expected -eq $actual) {
            Write-Host "  $tree matches ReXGlue $SdkCommit + sdk\patches (tree $($expected.Substring(0, 8)))"
            return
        }
        $differing = @(Get-TreeDiffPaths $tree $expected $actual)
        if (Test-SeriesContainedIn $tree $expected $actualIndex) {
            Write-Host "  $tree has changes beyond sdk\patches in:" -ForegroundColor Yellow
            Show-PathList $differing 'Yellow'
            return
        }
        Write-Host "$tree differs from ReXGlue $SdkCommit + sdk\patches in:" -ForegroundColor Red
        Show-PathList @(Select-SeriesPaths $tree $expected $differing) 'Red'
        Fail "$tree lacks part of sdk\patches or changes lines they add. $remedy"
    } finally {
        Restore-GitIndexVariable $previousIndex
        Remove-Item $expectedIndex, $actualIndex -Force -ErrorAction SilentlyContinue
    }
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

function ConvertFrom-MakePath([string]$text) {
    return $text.Replace('\ ', ' ')
}

# codegen.d is a Makefile rule: '<stamp>: \' then one input per line. "C:" is not the separator.
function Read-CodegenDeps([string]$file) {
    $text = (Get-Content $file -Raw) -replace '\\\r?\n', ' '
    $match = [regex]::Match($text, '^\s*(?<target>.+?):(\s|$)(?<inputs>.*)$', 'Singleline')
    if (-not $match.Success) { return $null }
    $inputs = [regex]::Matches($match.Groups['inputs'].Value, '(?:\\ |\S)+') | ForEach-Object { ConvertFrom-MakePath $_.Value }
    return @{ Target = (ConvertFrom-MakePath $match.Groups['target'].Value.Trim()); Inputs = @($inputs) }
}

function Get-CodegenRoot([string]$target) {
    return ($target -replace '/', '\') -replace '\\generated\\default\\codegen\.build\.stamp$', ''
}

function ConvertTo-FullPath([string]$path) {
    return [IO.Path]::GetFullPath(($path -replace '/', '\')).TrimEnd('\')
}

function Test-SamePath([string]$a, [string]$b) {
    return (ConvertTo-FullPath $a) -eq (ConvertTo-FullPath $b)
}

function Get-InputRelativePath([string]$path, [string]$codegenRoot) {
    $full = ConvertTo-FullPath $path
    $base = (ConvertTo-FullPath $codegenRoot) + '\'
    if (-not $full.StartsWith($base, [StringComparison]::OrdinalIgnoreCase)) { return $null }
    return $full.Substring($base.Length)
}

# Line endings do not change what codegen reads from a .toml file.
function Get-ContentHash([string]$path) {
    if ($path -notmatch '\.toml$') { return Get-FileSha256 $path }
    $text = [IO.File]::ReadAllText($path) -replace "`r`n", "`n"
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($text))).Replace('-', '') }
    finally { $sha.Dispose() }
}

function Get-InputProvenanceProblem([string]$path, [string]$otherRoot) {
    $relative = Get-InputRelativePath $path $otherRoot
    if (-not $relative) { return "$path is not under $otherRoot" }
    $local = Join-Path $root $relative
    if (-not (Test-Path $path -PathType Leaf)) { return "$path no longer exists" }
    if (-not (Test-Path $local -PathType Leaf)) { return "$relative is missing in this tree" }
    if ((Get-ContentHash $path) -ne (Get-ContentHash $local)) { return "$relative differs from $path" }
    return $null
}

function Get-CodegenProvenanceProblems([hashtable]$deps, [string]$otherRoot) {
    return @($deps.Inputs | ForEach-Object { Get-InputProvenanceProblem $_ $otherRoot } | Where-Object { $_ })
}

function Test-InputNewerThanStamp([string]$path, [datetime]$stampTime) {
    if (-not (Test-Path $path -PathType Leaf)) { return $false }
    $allowance = 0
    if ((Split-Path $path -Leaf) -eq 'gow2_manifest.toml') { $allowance = $ManifestRewriteSeconds }
    return (Get-Item $path).LastWriteTimeUtc -gt $stampTime.AddSeconds($allowance)
}

# codegen.build.stamp is rewritten by every codegen run; the outputs only when they change.
function Get-StaleCodegenInputs([hashtable]$deps) {
    $stampTime = (Get-Item $codegenStamp).LastWriteTimeUtc
    $codegenRoot = Get-CodegenRoot $deps.Target
    $stale = @()
    foreach ($path in $deps.Inputs) {
        $relative = Get-InputRelativePath $path $codegenRoot
        if ($relative -and (Test-InputNewerThanStamp (Join-Path $root $relative) $stampTime)) { $stale += $relative }
    }
    return $stale
}

# The SDK writes the configure time into librexruntime.so (string(TIMESTAMP)); CMake takes
# SOURCE_DATE_EPOCH instead, and the SDK commit's time keeps two builds of one commit identical.
function Set-SourceDateEpoch([string]$tree) {
    if ($env:SOURCE_DATE_EPOCH) { return }
    $epoch = (Invoke-TreeGit $tree @('log', '-1', '--format=%ct', $SdkCommit)).Out
    if ($epoch.Count -gt 0) { $env:SOURCE_DATE_EPOCH = $epoch[0] }
}

function Write-CodegenWarning([string]$problem) {
    Write-Host ("  warning: $problem; give -Sdk <folder> (bin\rexglue.exe) and game\default.xex to " +
        'regenerate it. Building the existing generated code.') -ForegroundColor Yellow
}

function Assert-SameCodegenInputs([hashtable]$deps, [string]$otherRoot) {
    $problems = @(Get-CodegenProvenanceProblems $deps $otherRoot)
    if ($problems.Count -eq 0) {
        Write-Host "  warning: generated\default was generated in $otherRoot from inputs identical to this tree's; building it." -ForegroundColor Yellow
        return
    }
    Write-Host "generated\default was generated in $otherRoot, and its inputs differ from this tree's:" -ForegroundColor Red
    Show-PathList $problems 'Red'
    Fail ('Give -Sdk <folder> (bin\rexglue.exe) and game\default.xex to regenerate it in this tree. ' +
        'Worktrees of different branches must not share one generated\ folder.')
}

# Without the codegen tools, codegen.d says where generated\default came from and what it read.
function Test-GeneratedCodeCurrent {
    if (-not ((Test-Path $codegenDeps) -and (Test-Path $codegenStamp))) {
        Write-CodegenWarning 'generated\default has no codegen.d or codegen.build.stamp, so its inputs cannot be checked'
        return
    }
    $deps = Read-CodegenDeps $codegenDeps
    if (-not $deps) {
        Write-CodegenWarning "$codegenDeps could not be read"
        return
    }
    $codegenRoot = Get-CodegenRoot $deps.Target
    if (-not (Test-SamePath $codegenRoot $root)) {
        Assert-SameCodegenInputs $deps $codegenRoot
        return
    }
    $stale = @(Get-StaleCodegenInputs $deps)
    if ($stale.Count -gt 0) { Write-CodegenWarning "generated\default is older than $($stale -join ', ')" }
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
    Step "Checking $SdkSource against ReXGlue $SdkCommit + sdk\patches"
    Assert-SdkTreeMatchesSeries $SdkSource ('If your tree is newer than sdk\patches, regenerate 0007 from ' +
        'it first (sdk\patches\README.md); otherwise run without -SdkSource or apply the missing patches.')
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
    Assert-SdkTreeMatchesSeries $SdkSource "Delete $cleanSdkMarker to prepare it again."
}

# ---- 1. codegen -----------------------------------------------------------------------------
$sourcesCmake = 'generated\default\sources.cmake'
if ($Codegen) {
    Invoke-Codegen 'forced with -Codegen'
} elseif (-not (Test-Path $sourcesCmake)) {
    Invoke-Codegen 'generated\default is missing'
} elseif (Test-CodegenToolsPresent $Sdk) {
    # A no-op when generated\default is current, and it records this tree's inputs in codegen.d.
    Invoke-Codegen 'checking generated\default; codegen skips the work when it is current'
} else {
    Step 'Checking generated\default against its codegen inputs'
    Test-GeneratedCodeCurrent
}

# ---- 2. Android copy of the generated code ---------------------------------------------------
Step 'Preparing the Android copy of the generated code'
Invoke-Step 'prepare-sources' $python @((Join-Path $root 'tools\prepare_android_sources.py'),
    '--source', (Join-Path $root 'generated\default'), '--dest', $genCopy)
Get-Content (Join-Path $logs 'prepare-sources.log') | Select-Object -Last 1 | ForEach-Object { Write-Host "  $_" }

# ---- 3. configure ----------------------------------------------------------------------------
Repair-SymlinkStubs $SdkSource
Set-SourceDateEpoch $SdkSource
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
# The repository root and the NDK are always forbidden build paths; an SDK tree elsewhere is too.
$forbidSdkPath = @('--forbid-path', $SdkSource)
Step "Checking the libraries in $abiLibs"
Invoke-Step 'check-libs' $python (@((Join-Path $root 'tools\check_android_libs.py'), $abiLibs,
    '--ndk', $Ndk, '--api', "$Api") + $forbidSdkPath)
Get-Content (Join-Path $logs 'check-libs.log') | Select-Object -Last 1 | ForEach-Object { Write-Host "  $_" }

if ($SkipGradle) {
    Write-Host "`nDone: native libraries in $abiLibs" -ForegroundColor Green
    exit 0
}

# ---- 7. Gradle -------------------------------------------------------------------------------
Step 'Building the APK (Gradle)'
Write-LocalProperties $androidSdk
# Gradle packs a debug APK incrementally into the old file, which kept every rebuild's dead space
# (113.5 MB file, 67.5 MB of entries); a fresh file also makes the missing-APK check below mean something.
if (Test-Path $apk) { Remove-Item $apk -Force }
Invoke-Step 'gradle' (Join-Path $root 'android\gradlew.bat') @('-p', (Join-Path $root 'android'),
    'assembleDebug', '-Pgow2.gameLibraries=true', "-Pgow2.gameLibs.dir=$jniLibs", '--console=plain')
if (-not (Test-Path $apk)) { Fail "Gradle reported success but $apk is missing." }

Step 'Checking the APK'
Invoke-Step 'check-apk' $python (@((Join-Path $root 'tools\check_android_libs.py'), $apk, '--ndk', $Ndk,
    '--api', "$Api") + $forbidSdkPath)
Get-Content (Join-Path $logs 'check-apk.log') | Select-Object -Last 1 | ForEach-Object { Write-Host "  $_" }

Write-Host "`nDone: $apk" -ForegroundColor Green
Write-Host 'Install with: adb install -r android\app\build\outputs\apk\debug\app-debug.apk'
