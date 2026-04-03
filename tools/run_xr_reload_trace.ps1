param(
	[string]$GodotExe = 'S:\Godot\Godot_v4.6.1-stable_win64_console.exe',
	[string]$ProjectDir = 'S:\code\godot-libmpv-zero\project',
	[string]$ScenePath = 'res://example_vr.tscn',
	[string]$LogDir = 'S:\code\godot-libmpv-zero\build-phase0\logs',
	[Alias('Media-Source')]
	[string]$MediaSource = '',
	[double]$ReloadAfterSeconds = 2.0,
	[int]$TimeoutSeconds = 20,
	[switch]$ShowLogs
)

$ErrorActionPreference = 'Stop'

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$stdoutLog = Join-Path $LogDir "xr-reload-$timestamp.stdout.log"
$stderrLog = Join-Path $LogDir "xr-reload-$timestamp.stderr.log"

$previousTraceLoad = $env:LIBMPV_ZERO_TRACE_LOAD
$previousTraceFrameGaps = $env:LIBMPV_ZERO_TRACE_FRAME_GAPS
$previousMediaSource = $env:LIBMPV_ZERO_MEDIA
$previousReloadAfter = $env:LIBMPV_ZERO_RELOAD_AFTER
$previousAutoQuit = $env:LIBMPV_ZERO_AUTOQUIT
$env:LIBMPV_ZERO_TRACE_LOAD = '1'
$env:LIBMPV_ZERO_TRACE_FRAME_GAPS = '1'
$env:LIBMPV_ZERO_RELOAD_AFTER = [string]$ReloadAfterSeconds
$env:LIBMPV_ZERO_AUTOQUIT = '1'
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
	if ($null -ne $previousTraceLoad) {
		$env:LIBMPV_ZERO_TRACE_LOAD = $previousTraceLoad
	} else {
		Remove-Item Env:LIBMPV_ZERO_TRACE_LOAD -ErrorAction SilentlyContinue
	}
	if ($null -ne $previousTraceFrameGaps) {
		$env:LIBMPV_ZERO_TRACE_FRAME_GAPS = $previousTraceFrameGaps
	} else {
		Remove-Item Env:LIBMPV_ZERO_TRACE_FRAME_GAPS -ErrorAction SilentlyContinue
	}
	if ($null -ne $previousMediaSource) {
		$env:LIBMPV_ZERO_MEDIA = $previousMediaSource
	} else {
		Remove-Item Env:LIBMPV_ZERO_MEDIA -ErrorAction SilentlyContinue
	}
	if ($null -ne $previousReloadAfter) {
		$env:LIBMPV_ZERO_RELOAD_AFTER = $previousReloadAfter
	} else {
		Remove-Item Env:LIBMPV_ZERO_RELOAD_AFTER -ErrorAction SilentlyContinue
	}
	if ($null -ne $previousAutoQuit) {
		$env:LIBMPV_ZERO_AUTOQUIT = $previousAutoQuit
	} else {
		Remove-Item Env:LIBMPV_ZERO_AUTOQUIT -ErrorAction SilentlyContinue
	}
}

Write-Output "stdout: $stdoutLog"
Write-Output "stderr: $stderrLog"
Write-Output "scene: $ScenePath"
if ($MediaSource) {
	Write-Output "media: $MediaSource"
}
Write-Output "reload_after: $ReloadAfterSeconds"

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
