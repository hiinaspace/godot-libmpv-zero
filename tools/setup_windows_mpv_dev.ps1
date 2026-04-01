param(
    [string]$ReleaseTag = "20260331",
    [string]$GitHash = "9465b30"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$dependenciesDir = Join-Path $repoRoot "dependencies"
$mpvDevDir = Join-Path $dependenciesDir "mpv-dev"
$archiveName = "mpv-dev-x86_64-$ReleaseTag-git-$GitHash.7z"
$downloadUrl = "https://github.com/shinchiro/mpv-winbuild-cmake/releases/download/$ReleaseTag/$archiveName"
$archivePath = Join-Path $dependenciesDir $archiveName
$runtimeDll = Join-Path $mpvDevDir "libmpv-2.dll"
$headerPath = Join-Path $mpvDevDir "include\\mpv\\client.h"
$projectRuntimeDir = Join-Path $repoRoot "project\\bin\\windows"

New-Item -ItemType Directory -Force -Path $dependenciesDir | Out-Null

if (-not ((Test-Path $runtimeDll) -and (Test-Path $headerPath))) {
    Write-Host "Downloading $archiveName"
    Invoke-WebRequest -Uri $downloadUrl -OutFile $archivePath

    try {
        New-Item -ItemType Directory -Force -Path $mpvDevDir | Out-Null
        tar.exe -xf $archivePath -C $mpvDevDir
    }
    finally {
        if (Test-Path $archivePath) {
            Remove-Item -LiteralPath $archivePath -Force
        }
    }
}

if (-not (Test-Path $runtimeDll)) {
    throw "Expected libmpv runtime DLL not found after extraction: $runtimeDll"
}

New-Item -ItemType Directory -Force -Path $projectRuntimeDir | Out-Null
Copy-Item -LiteralPath $runtimeDll -Destination (Join-Path $projectRuntimeDir "libmpv-2.dll") -Force

Write-Host "libmpv runtime staged at $projectRuntimeDir"
