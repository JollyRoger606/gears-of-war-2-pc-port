<#
.SYNOPSIS
  Builds the ReXGlue SDK runtime DLLs the game runs against.

.DESCRIPTION
  1. Clones the ReXGlue SDK at the pinned commit this project is built against,
  2. applies sdk\patches\*.patch on top of it,
  3. fetches the public NVIDIA DLSS SDK and the AMD FidelityFX-SDK v2.3.0
     (API headers plus the signed loader/upscaler binaries; nothing is redistributed
     from our tree),
  4. configures it with the win-amd64 preset plus -DREXGLUE_ENABLE_FIDELITYFX=ON
     (the SDK's cmake/rexglue_fidelityfx.cmake fetches its FidelityFX fork itself)
     and the GOW2_DLSS_SDK / GOW2_FFX_SDK paths for the temporal upscalers
     (sdk\patches\0002-temporal-upscalers.patch),
  5. builds the rexruntime and rexgpu-xenos targets with the release build preset,
  6. copies rexruntime.dll, rexgpu-xenos.dll, nvngx_dlss.dll and the
     amd_fidelityfx_loader/upscaler_dx12.dll files into out\sdk\.

  scripts\build.ps1 then picks the DLLs up automatically (see CMakeLists.txt).
  Without them the game still builds and runs with the stock SDK DLLs.

  The first run downloads the SDK with its third-party libraries and compiles it;
  expect 10 to 20 minutes and several GB of disk space in out\sdk-build\.

.PARAMETER Dev
  Also build the SDK development tools (frame tracing, dumps, the D3D12 debug
  layer switch: -DGOW2_DEV_TOOLS=ON).

.PARAMETER Clean
  Delete out\sdk-build\ and out\sdk\ first.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\build-sdk.ps1
#>
param(
    [switch]$Dev,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# The SDK commit the project and the patches are made for (must match the
# prebuilt SDK the game is compiled against).
$SdkUrl = 'https://github.com/rexglue/rexglue-sdk.git'
$SdkCommit = '923c1a5'

function Fail($msg) { Write-Host "ERROR: $msg" -ForegroundColor Red; exit 1 }
function Step($msg) { Write-Host "`n== $msg" -ForegroundColor Cyan }
function Run($what, [scriptblock]$cmd) {
    & $cmd
    if ($LASTEXITCODE -ne 0) { Fail "$what failed (exit code $LASTEXITCODE)." }
}

function Find-VsWithMsvcHeaders {
    # Some VS installs ship cl.exe without the toolset headers (excpt.h); pick one that has them.
    # NOTE: do not use `vswhere -latest` here; the latest VS install on this machine
    # has a broken MSVC toolset.
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
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

. (Join-Path $PSScriptRoot 'sdk-symlinks.ps1')

function Find-ClangDir($vsPath) {
    foreach ($dir in @((Join-Path $vsPath 'VC\Tools\Llvm\x64\bin'), (Join-Path $env:ProgramFiles 'LLVM\bin'))) {
        if (Test-Path (Join-Path $dir 'clang++.exe')) { return $dir }
    }
    $onPath = Get-Command clang++ -ErrorAction SilentlyContinue
    if ($onPath) { return Split-Path $onPath.Source }
    return $null
}

foreach ($tool in 'git', 'cmake', 'ninja') {
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

$work = Join-Path $root 'out\sdk-build'
$outDir = Join-Path $root 'out\sdk'
if ($Clean) {
    Step 'Cleaning'
    foreach ($d in $work, $outDir) { if (Test-Path $d) { Remove-Item $d -Recurse -Force } }
}
New-Item -ItemType Directory -Force $work, $outDir | Out-Null

# ---- the SDK ---------------------------------------------------------------------------------
$sdk = Join-Path $work 'rexglue-sdk'
$marker = Join-Path $work 'sdk-patched.txt'
$patchDir = Join-Path $root 'sdk\patches'
$patchFiles = @()
if (Test-Path $patchDir) { $patchFiles = @(Get-ChildItem $patchDir -Filter *.patch | Sort-Object Name) }
$stamp = ($patchFiles | ForEach-Object { "$($_.Name):$((Get-FileHash $_.FullName -Algorithm SHA256).Hash)" }) -join ','
$want = "$SdkCommit|$stamp"

if (-not (Test-Path (Join-Path $sdk '.git'))) {
    Step "Cloning the ReXGlue SDK ($SdkCommit)"
    Run 'git clone' { git clone --filter=blob:none $SdkUrl $sdk }
}
if (-not (Test-Path $marker) -or (Get-Content $marker -Raw).Trim() -ne $want) {
    Step 'Preparing the SDK sources'
    Push-Location $sdk
    try {
        Run 'git checkout' { git checkout -f $SdkCommit }
        Run 'git clean' { git clean -fdq -e out }
        Run 'git submodule update' { git submodule update --init --recursive --depth 1 --jobs 8 }
        Repair-SymlinkStubs $sdk
        foreach ($p in $patchFiles) {
            Run "applying patch $($p.Name)" { git apply --ignore-whitespace --whitespace=nowarn $p.FullName }
        }
    } finally { Pop-Location }
    Set-Content $marker $want
}

# ---- public upscaler SDKs (not redistributed) --------------------------------------------------
$dlss = Join-Path $work 'dlss-sdk'
if (-not (Test-Path (Join-Path $dlss 'include\nvsdk_ngx.h'))) {
    Step 'Fetching the NVIDIA DLSS SDK headers'
    Run 'git clone DLSS' { git clone --depth 1 https://github.com/NVIDIA/DLSS.git $dlss }
}
$ffx = Join-Path $work 'ffx-sdk'
if (-not (Test-Path (Join-Path $ffx 'Kits\FidelityFX\signedbin\amd_fidelityfx_upscaler_dx12.dll'))) {
    Step 'Fetching the AMD FidelityFX-SDK (v2.3.0: API headers and signed binaries)'
    if (-not (Test-Path (Join-Path $ffx '.git'))) {
        Run 'git clone FidelityFX' { git clone --depth 1 --branch v2.3.0 --filter=blob:none --sparse https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK.git $ffx }
    }
    Push-Location $ffx
    try {
        Run 'sparse checkout' { git sparse-checkout set Kits/FidelityFX/api/include Kits/FidelityFX/upscalers/include Kits/FidelityFX/signedbin }
    } finally { Pop-Location }
}

# ---- build -------------------------------------------------------------------------------------
# Also on reuse: a previous run may have prepared the tree before this repair existed.
Repair-SymlinkStubs $sdk

Step 'Configuring'
$devFlag = if ($Dev) { 'ON' } else { 'OFF' }
Push-Location $sdk
try {
    Run 'cmake configure' {
        cmake --preset win-amd64 -DREXGLUE_ENABLE_FIDELITYFX=ON "-DGOW2_DEV_TOOLS=$devFlag" `
            "-DGOW2_DLSS_SDK=$($dlss.Replace([string][char]92, '/'))" `
            "-DGOW2_FFX_SDK=$($ffx.Replace([string][char]92, '/'))/Kits/FidelityFX"
    }
    Step 'Building rexruntime.dll and rexgpu-xenos.dll (this takes a while)'
    Run 'cmake build' { cmake --build --preset win-amd64-release --target rexruntime rexgpu-xenos }
} finally { Pop-Location }

# ---- install -----------------------------------------------------------------------------------
Step 'Installing the DLLs'
$sdkOut = Join-Path $sdk 'out'
# The game build copies every DLL found here, so drop what an older run left behind.
Get-ChildItem $outDir -Filter *.dll | Remove-Item -Force
foreach ($dll in 'rexruntime.dll', 'rexgpu-xenos.dll') {
    # The SDK is multi-config (out\...\<Config>\); prefer the Release output.
    $src = Get-ChildItem $sdkOut -Recurse -Filter $dll -ErrorAction SilentlyContinue |
        Sort-Object { $_.FullName -notlike '*Release*' } |
        Select-Object -First 1
    if (-not $src) { Fail "$dll was not produced under $sdkOut." }
    Copy-Item $src.FullName $outDir -Force
    Write-Host "  $dll <- $($src.FullName)"
}
# The official, signed temporal-upscaler libraries the game loads at run time (redistribution is
# permitted by both vendors; see THIRD_PARTY_NOTICES.md).
$ngx = Join-Path $dlss 'lib\Windows_x86_64\rel\nvngx_dlss.dll'
if (-not (Test-Path $ngx)) { Fail "nvngx_dlss.dll was not produced in the DLSS SDK ($ngx)." }
Copy-Item $ngx $outDir -Force
Write-Host "  nvngx_dlss.dll <- $ngx"
foreach ($n in 'loader', 'upscaler') {
    $f = Join-Path $ffx "Kits\FidelityFX\signedbin\amd_fidelityfx_${n}_dx12.dll"
    if (-not (Test-Path $f)) { Fail "$(Split-Path $f -Leaf) was not found in the FidelityFX-SDK ($f)." }
    Copy-Item $f $outDir -Force
    Write-Host "  $(Split-Path $f -Leaf) <- $f"
}
# rexruntime.dll imports amd_fidelityfx_dx12.dll (FSR 1 / CAS present effects); the other signed
# FidelityFX libraries (frame generation, denoiser, radiance cache, Vulkan) are not used.
# The one the SDK builds next to its import library (bin\), not AMD's prebuilt copy under _deps\.
$ffxRuntime = Get-Item (Join-Path $sdk 'bin\amd_fidelityfx_dx12.dll') -ErrorAction SilentlyContinue
if (-not $ffxRuntime) { Fail "amd_fidelityfx_dx12.dll was not built in $sdk\bin (REXGLUE_ENABLE_FIDELITYFX build)." }
Copy-Item $ffxRuntime.FullName $outDir -Force
Write-Host "  amd_fidelityfx_dx12.dll <- $($ffxRuntime.FullName)"

Step 'Done'
Write-Host "Self-built SDK DLLs: $outDir" -ForegroundColor Green
Get-ChildItem $outDir -Filter *.dll | ForEach-Object { Write-Host "  $($_.Name)" }
Write-Host 'Run scripts\build.ps1 to build the game with them.'
