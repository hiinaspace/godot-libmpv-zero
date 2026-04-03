param(
	[string]$GodotExe = 'S:\Godot\Godot_v4.6.1-stable_win64_console.exe',
	[string]$ProjectDir = 'S:\code\godot-libmpv-zero\project',
	[string]$LogDir = 'S:\code\godot-libmpv-zero\build-phase0\logs',
	[Alias('Media-Source')]
	[string]$MediaSource = '',
	[ValidateSet('default', 'off', 'on')]
	[string]$XrMode = 'off',
	[double]$ReloadAfterSeconds = 0,
	[int]$TimeoutSeconds = 0,
	[switch]$ShowFiltered
)

$ErrorActionPreference = 'Stop'

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$stdoutLog = Join-Path $LogDir "sample-$timestamp.stdout.log"
$stderrLog = Join-Path $LogDir "sample-$timestamp.stderr.log"

$previousAutoQuit = $env:LIBMPV_ZERO_AUTOQUIT
$previousMediaSource = $env:LIBMPV_ZERO_MEDIA
$previousReloadAfter = $env:LIBMPV_ZERO_RELOAD_AFTER
$env:LIBMPV_ZERO_AUTOQUIT = '1'
if ($MediaSource) {
	$env:LIBMPV_ZERO_MEDIA = $MediaSource
}
if ($ReloadAfterSeconds -gt 0) {
	$env:LIBMPV_ZERO_RELOAD_AFTER = [string]$ReloadAfterSeconds
}

try {
	$argumentList = @('--xr-mode', $XrMode, '--path', $ProjectDir)
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
	if ($null -ne $previousAutoQuit) {
		$env:LIBMPV_ZERO_AUTOQUIT = $previousAutoQuit
	} else {
		Remove-Item Env:LIBMPV_ZERO_AUTOQUIT -ErrorAction SilentlyContinue
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
}

Write-Output "stdout: $stdoutLog"
Write-Output "stderr: $stderrLog"
if ($MediaSource) {
	Write-Output "media: $MediaSource"
}
Write-Output "xr_mode: $XrMode"
if ($ReloadAfterSeconds -gt 0) {
	Write-Output "reload_after: $ReloadAfterSeconds"
}

if ($ShowFiltered) {
	$pattern = 'audio diag|audio per-channel|video status|video_size_changed|file_loaded|playback_finished|auto quit|example\.gd perf'
	Write-Output '--- filtered stdout ---'
	if (Test-Path $stdoutLog) {
		Get-Content $stdoutLog | Select-String -Pattern $pattern
	}
	Write-Output '--- stderr ---'
	if (Test-Path $stderrLog) {
		Get-Content $stderrLog
	}
}
