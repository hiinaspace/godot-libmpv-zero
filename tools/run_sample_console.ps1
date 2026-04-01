param(
	[string]$GodotExe = 'S:\Godot\Godot_v4.6.1-stable_win64_console.exe',
	[string]$ProjectDir = 'S:\code\godot-libmpv-zero\project',
	[string]$LogDir = 'S:\code\godot-libmpv-zero\build-phase0\logs',
	[int]$TimeoutSeconds = 25,
	[switch]$ShowFiltered
)

$ErrorActionPreference = 'Stop'

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$stdoutLog = Join-Path $LogDir "sample-$timestamp.stdout.log"
$stderrLog = Join-Path $LogDir "sample-$timestamp.stderr.log"

$previousAutoQuit = $env:LIBMPV_ZERO_AUTOQUIT
$env:LIBMPV_ZERO_AUTOQUIT = '1'

try {
	$process = Start-Process `
		-FilePath $GodotExe `
		-ArgumentList '--path', $ProjectDir `
		-WorkingDirectory (Split-Path $ProjectDir -Parent) `
		-RedirectStandardOutput $stdoutLog `
		-RedirectStandardError $stderrLog `
		-PassThru

	if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
		Stop-Process -Id $process.Id -Force
		Write-Warning "Timed out after $TimeoutSeconds seconds and stopped Godot."
	}
} finally {
	if ($null -ne $previousAutoQuit) {
		$env:LIBMPV_ZERO_AUTOQUIT = $previousAutoQuit
	} else {
		Remove-Item Env:LIBMPV_ZERO_AUTOQUIT -ErrorAction SilentlyContinue
	}
}

Write-Output "stdout: $stdoutLog"
Write-Output "stderr: $stderrLog"

if ($ShowFiltered) {
	$pattern = 'audio diag|audio per-channel|video status|video_size_changed|file_loaded|playback_finished|auto quit'
	Write-Output '--- filtered stdout ---'
	if (Test-Path $stdoutLog) {
		Get-Content $stdoutLog | Select-String -Pattern $pattern
	}
	Write-Output '--- stderr ---'
	if (Test-Path $stderrLog) {
		Get-Content $stderrLog
	}
}
