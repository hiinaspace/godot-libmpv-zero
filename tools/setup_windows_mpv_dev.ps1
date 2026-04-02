param(
    [string]$Repo = "hiinaspace/mpv-winbuild-cmake",
    [string]$WorkflowName = "mpv clang",
    [long]$RunId = 0,
    [string]$ArtifactRuntime = "mpv-x86_64",
    [string]$ArtifactDev = "mpv-dev-x86_64",
    [string]$Branch = "master",
    [string]$StageDir = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Get-LatestRunWithArtifacts {
    param(
        [string]$Repository,
        [string]$Workflow,
        [string]$RuntimeArtifact,
        [string]$DevArtifact,
        [string]$HeadBranch
    )

    $json = gh run list -R $Repository --workflow $Workflow --branch $HeadBranch --limit 20 --json databaseId,status,conclusion,headBranch | ConvertFrom-Json
    $completedRuns = $json | Where-Object {
        $_.status -eq "completed" -and
        $_.conclusion -eq "success" -and
        $_.headBranch -eq $HeadBranch
    }
    foreach ($run in $completedRuns) {
        $artifacts = Get-ArtifactMap -Repository $Repository -WorkflowRunId ([long]$run.databaseId)
        if ($artifacts.ContainsKey($RuntimeArtifact) -and $artifacts.ContainsKey($DevArtifact)) {
            return [long]$run.databaseId
        }
    }

    throw "No successful runs with '$RuntimeArtifact' and '$DevArtifact' artifacts found for workflow '$Workflow' on branch '$HeadBranch' in '$Repository'"
}

function Get-ArtifactMap {
    param(
        [string]$Repository,
        [long]$WorkflowRunId
    )

    $response = gh api "repos/$Repository/actions/runs/$WorkflowRunId/artifacts" | ConvertFrom-Json
    $artifacts = @{}
    foreach ($artifact in $response.artifacts) {
        if (-not $artifact.expired) {
            $artifacts[$artifact.name] = $artifact.archive_download_url
        }
    }
    return $artifacts
}

function Download-ArtifactZip {
    param(
        [string]$Url,
        [string]$Destination
    )

    Invoke-WebRequest -Headers @{ Authorization = "Bearer $(gh auth token)" } -Uri $Url -OutFile $Destination
}

function Expand-NestedArchive {
    param(
        [string]$ZipPath,
        [string]$Destination
    )

    $outerDir = Join-Path $Destination "outer"
    $innerDir = Join-Path $Destination "inner"
    New-Item -ItemType Directory -Force -Path $outerDir | Out-Null
    New-Item -ItemType Directory -Force -Path $innerDir | Out-Null

    7z x $ZipPath "-o$outerDir" -y | Out-Null
    $innerArchive = Get-ChildItem -Path $outerDir -Filter *.7z | Select-Object -First 1
    if (-not $innerArchive) {
        throw "Expected nested .7z archive inside $ZipPath"
    }

    7z x $innerArchive.FullName "-o$innerDir" -y | Out-Null
    return $innerDir
}

if ([string]::IsNullOrWhiteSpace($StageDir)) {
    $repoRoot = Split-Path -Parent $PSScriptRoot
    $StageDir = Join-Path $repoRoot "dependencies\mpv-dev"
}

if ($RunId -le 0) {
    $RunId = Get-LatestRunWithArtifacts -Repository $Repo -Workflow $WorkflowName -RuntimeArtifact $ArtifactRuntime -DevArtifact $ArtifactDev -HeadBranch $Branch
}

$artifactMap = Get-ArtifactMap -Repository $Repo -WorkflowRunId $RunId
if (-not $artifactMap.ContainsKey($ArtifactDev)) {
    throw "Artifact '$ArtifactDev' not found for run $RunId in '$Repo'"
}
if (-not $artifactMap.ContainsKey($ArtifactRuntime)) {
    throw "Artifact '$ArtifactRuntime' not found for run $RunId in '$Repo'"
}

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("libmpv-zero-" + [guid]::NewGuid().ToString("N"))
$zipDir = Join-Path $tempRoot "zips"
$extractDir = Join-Path $tempRoot "extract"
New-Item -ItemType Directory -Force -Path $zipDir | Out-Null
New-Item -ItemType Directory -Force -Path $extractDir | Out-Null

try {
    $devZip = Join-Path $zipDir "$ArtifactDev.zip"
    $runtimeZip = Join-Path $zipDir "$ArtifactRuntime.zip"

    Download-ArtifactZip -Url $artifactMap[$ArtifactDev] -Destination $devZip
    Download-ArtifactZip -Url $artifactMap[$ArtifactRuntime] -Destination $runtimeZip

    $devExtract = Expand-NestedArchive -ZipPath $devZip -Destination (Join-Path $extractDir "dev")
    $runtimeExtract = Expand-NestedArchive -ZipPath $runtimeZip -Destination (Join-Path $extractDir "runtime")

    $stageBin = Join-Path $StageDir "bin"
    $stageLib = Join-Path $StageDir "lib"
    $stageInclude = Join-Path $StageDir "include"

    if (Test-Path $StageDir) {
        Remove-Item -Recurse -Force $StageDir
    }

    New-Item -ItemType Directory -Force -Path $stageBin | Out-Null
    New-Item -ItemType Directory -Force -Path $stageLib | Out-Null
    New-Item -ItemType Directory -Force -Path $stageInclude | Out-Null

    Copy-Item -Recurse -Force (Join-Path $devExtract "include\*") $stageInclude
    Copy-Item -LiteralPath (Join-Path $devExtract "libmpv.dll.a") -Destination (Join-Path $stageLib "libmpv.dll.a") -Force
    Copy-Item -LiteralPath (Join-Path $devExtract "libmpv-2.dll") -Destination (Join-Path $stageBin "libmpv-2.dll") -Force

    Get-ChildItem -Path $runtimeExtract -Recurse -Filter *.dll | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $stageBin $_.Name) -Force
    }

    Write-Host "Staged CI libmpv package at $StageDir from run $RunId"
}
finally {
    if (Test-Path $tempRoot) {
        Remove-Item -Recurse -Force $tempRoot
    }
}
