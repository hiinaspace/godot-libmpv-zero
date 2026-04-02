param(
    [string]$MpvSource = "S:\lib\mpv",
    [string]$BuildDir = "build-clang64",
    [string]$StageDir = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$setupScript = Join-Path $PSScriptRoot "setup_mpv_build_env.ps1"
$envInfo = & $setupScript

if (-not (Test-Path $MpvSource)) {
    throw "mpv source tree not found: $MpvSource"
}

if ([string]::IsNullOrWhiteSpace($StageDir)) {
    $repoRoot = Split-Path -Parent $PSScriptRoot
    $StageDir = Join-Path $repoRoot "dependencies\mpv-dev"
}

$mpvSourceUnix = $MpvSource -replace '^([A-Za-z]):', '/$1' -replace '\\', '/'
$buildDirUnix = $BuildDir -replace '\\', '/'
$stageDirUnix = $StageDir -replace '^([A-Za-z]):', '/$1' -replace '\\', '/'

$script = @'
set -euo pipefail
export PATH=/clang64/bin:/usr/bin:$PATH
cd '__MPV_SOURCE__'
meson setup '__BUILD_DIR__' \
  -Dlibmpv=true \
  -Dcplayer=false \
  -Dvulkan=enabled \
  -Dgl=disabled \
  -Dd3d11=disabled \
  --prefix='__STAGE_DIR__'
meson compile -C '__BUILD_DIR__'
meson install -C '__BUILD_DIR__'
ldd '__STAGE_DIR__/bin/libmpv-2.dll' | awk '/=> \/clang64\/bin\// { print $3 }' | sort -u > '__STAGE_DIR__/bin/runtime_deps.txt'
'@
$script = $script.Replace("__MPV_SOURCE__", $mpvSourceUnix).Replace("__BUILD_DIR__", $buildDirUnix).Replace("__STAGE_DIR__", $stageDirUnix)

& $envInfo.BashExe -lc $script

$stagedDll = Join-Path $StageDir "bin\libmpv-2.dll"
if (-not (Test-Path $stagedDll)) {
    throw "Expected staged DLL not found: $stagedDll"
}

$runtimeDepsFile = Join-Path $StageDir "bin\runtime_deps.txt"
if (-not (Test-Path $runtimeDepsFile)) {
    throw "Expected runtime dependency list not found: $runtimeDepsFile"
}

Get-Content $runtimeDepsFile | ForEach-Object {
    if ([string]::IsNullOrWhiteSpace($_)) {
        return
    }
    $windowsPath = $_
    if ($windowsPath.StartsWith('/clang64/')) {
        $windowsPath = 'C:\msys64' + ($windowsPath -replace '/', '\')
    } elseif ($windowsPath.StartsWith('/usr/')) {
        $windowsPath = 'C:\msys64' + ($windowsPath -replace '/', '\')
    } else {
        $windowsPath = $windowsPath -replace '^/([A-Za-z])/', '$1:/' -replace '/', '\'
    }
    Copy-Item -LiteralPath $windowsPath -Destination (Join-Path $StageDir "bin") -Force
}

Remove-Item -LiteralPath $runtimeDepsFile -Force

Write-Host "Staged libmpv build at $StageDir"
