<#
.SYNOPSIS
  Compiles work-in-progress SDK changes without a full SDK build.

.DESCRIPTION
  Applies the changes of an rexglue-sdk worktree (relative to -Base) to the build tree that
  scripts\build-sdk.ps1 prepared (out\sdk-build\rexglue-sdk) and builds rexruntime and
  rexgpu-xenos incrementally. The previous run's changes are taken out first. Nothing is copied
  to out\sdk; run build-sdk.ps1 for the real DLLs once the patch is in sdk\patches.

.PARAMETER Worktree
  The rexglue-sdk worktree with the changes.

.PARAMETER Base
  Commit the changes are relative to (default HEAD of the worktree).

.PARAMETER Exclude
  Worktree paths left out of the patch.
#>
param(
    [Parameter(Mandatory)][string]$Worktree,
    [string]$Base = 'HEAD',
    [string[]]$Exclude = @('TEXTURE_REPLACEMENT_DESIGN.md', 'tmp-patch')
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$sdk = Join-Path $root 'out\sdk-build\rexglue-sdk'
$applied = Join-Path $root 'out\sdk-build\verify-applied.patch'

function Fail($msg) { Write-Host "VERIFY FAILED: $msg"; exit 1 }

function Find-VsWithMsvcHeaders {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $paths = & $vswhere -all -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    foreach ($p in $paths) {
        if (Get-ChildItem -Path (Join-Path $p 'VC\Tools\MSVC\*\include\excpt.h') -ErrorAction SilentlyContinue) { return $p }
    }
    return $null
}

function Import-VcVars($vsPath) {
    cmd /c "`"$(Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat')`" >nul && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
    }
}

function Undo-PreviousRun {
    if (-not (Test-Path $applied)) { return }
    # build-sdk.ps1 re-prepares the tree when the patches change; then there is nothing to undo.
    git -C $sdk apply -R --check $applied 2>$null
    if ($LASTEXITCODE -eq 0) { git -C $sdk apply -R $applied }
    Remove-Item $applied
}

function Write-WorktreePatch {
    $pathspec = @('.') + ($Exclude | ForEach-Object { ":(exclude)$_" })
    # New files only show up in `git diff` once they are in the index (intent-to-add).
    git -C $Worktree add -N -A -- @pathspec
    git -C $Worktree diff --binary "--output=$applied" $Base -- @pathspec
    if ((Get-Item $applied).Length -eq 0) { Remove-Item $applied; Fail 'the worktree has no changes.' }
}

if (-not (Test-Path (Join-Path $sdk 'out'))) { Fail "no prepared build tree in $sdk; run scripts\build-sdk.ps1 once." }
$Worktree = (Resolve-Path $Worktree).Path

Undo-PreviousRun
Write-WorktreePatch
git -C $sdk apply --check $applied 2>&1 | Write-Host
if ($LASTEXITCODE -ne 0) { Remove-Item $applied; Fail 'the changes do not apply to the build tree (see above).' }
git -C $sdk apply $applied

$vs = Find-VsWithMsvcHeaders
if (-not $vs) { Fail 'no Visual Studio / Build Tools install with a complete MSVC toolset.' }
Import-VcVars $vs
$env:PATH = "$env:ProgramFiles\LLVM\bin;$env:PATH"

Push-Location $sdk
$log = cmake --build --preset win-amd64-release --target rexruntime rexgpu-xenos 2>&1 | ForEach-Object { "$_" }
$code = $LASTEXITCODE
Pop-Location

# Only the errors: the full log is mostly warnings from third-party headers.
$log | Select-String -Pattern 'error:|error [A-Z]+\d+:|FAILED:|undefined symbol|ninja: build stopped' -Context 0, 4 |
    Select-Object -First 40 | ForEach-Object { Write-Host $_ }
if ($code -ne 0) { Fail "the build failed (exit code $code)." }
Write-Host 'VERIFY OK: rexruntime and rexgpu-xenos build with the worktree changes.'
