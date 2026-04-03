param(
	[string]$GodotExe = 'S:\Godot\Godot_v4.6.1-stable_win64_console.exe',
	[string]$ProjectDir = 'S:\code\godot-libmpv-zero\project',
	[string]$ScenePath = 'res://example_vr.tscn',
	[string]$LogDir = 'S:\code\godot-libmpv-zero\build-phase0\logs',
	[Alias('Media-Source')]
	[string]$MediaSource = '',
	[int]$TimeoutSeconds = 0,
	[switch]$ShowLogs
)

$ErrorActionPreference = 'Stop'

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$stdoutLog = Join-Path $LogDir "xr-seek-$timestamp.stdout.log"
$stderrLog = Join-Path $LogDir "xr-seek-$timestamp.stderr.log"

$previousTraceSeek = $env:LIBMPV_ZERO_TRACE_SEEK
$previousMediaSource = $env:LIBMPV_ZERO_MEDIA
$env:LIBMPV_ZERO_TRACE_SEEK = '1'
if ($MediaSource) {
	$env:LIBMPV_ZERO_MEDIA = $MediaSource
}

try {
	$argumentList = @('--xr-mode', 'on', '--path', $ProjectDir, '--scene', $ScenePath)
	if ($MediaSource) {
		$argumentList += @('--', "--media=$MediaSource")
	}

	$process = Start-Process `
		-FilePath $GodotExe `
		-ArgumentList $argumentList `
		-WorkingDirectory (Split-Path $ProjectDir -Parent) `
		-RedirectStandardOutput $stdoutLog `
		-RedirectStandardError $stderrLog `
		-PassThru

	if ($TimeoutSeconds -gt 0) {
		if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
			Stop-Process -Id $process.Id -Force
			Write-Warning "Timed out after $TimeoutSeconds seconds and stopped Godot."
		}
	} else {
		$process.WaitForExit()
	}
} finally {
	if ($null -ne $previousTraceSeek) {
		$env:LIBMPV_ZERO_TRACE_SEEK = $previousTraceSeek
	} else {
		Remove-Item Env:LIBMPV_ZERO_TRACE_SEEK -ErrorAction SilentlyContinue
	}
	if ($null -ne $previousMediaSource) {
		$env:LIBMPV_ZERO_MEDIA = $previousMediaSource
	} else {
		Remove-Item Env:LIBMPV_ZERO_MEDIA -ErrorAction SilentlyContinue
	}
}

Write-Output "stdout: $stdoutLog"
Write-Output "stderr: $stderrLog"
Write-Output "scene: $ScenePath"
if ($MediaSource) {
	Write-Output "media: $MediaSource"
}

if ($ShowLogs) {
	Write-Output '--- stdout ---'
	if (Test-Path $stdoutLog) {
		Get-Content $stdoutLog
	}
	Write-Output '--- stderr ---'
	if (Test-Path $stderrLog) {
		Get-Content $stderrLog
	}
}
