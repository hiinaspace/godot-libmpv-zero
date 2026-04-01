param()

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Get-RepoRoot {
	return Split-Path -Parent $PSScriptRoot
}

$msysRoot = "C:\msys64"
$bashExe = Join-Path $msysRoot "usr\bin\bash.exe"
if (-not (Test-Path $bashExe)) {
	throw "MSYS2 bash not found at $bashExe"
}

$clangBin = Join-Path $msysRoot "clang64\bin"
if (-not (Test-Path (Join-Path $clangBin "clang.exe"))) {
	throw "clang64 toolchain not found at $clangBin"
}

[pscustomobject]@{
	RepoRoot = Get-RepoRoot
	MsysRoot = $msysRoot
	BashExe = $bashExe
	ClangBin = $clangBin
}
