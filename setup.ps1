[CmdletBinding()]
param(
    [string]$GameDirectory,
    [string]$RuntimeDirectory,
    [ValidateSet('full','scripts')][string]$Mode = 'full',
    [switch]$InstallPrerequisites,
    [switch]$NonInteractive,
    [switch]$ConfigureOnly,
    [string]$ToolCache
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
function Select-Directory([string]$Title) {
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
    $dialog.Description = $Title
    $dialog.ShowNewFolderButton = $false
    try {
        if ($dialog.ShowDialog() -ne 'OK') { throw "Cancelled: $Title" }
        return $dialog.SelectedPath
    } finally { $dialog.Dispose() }
}
if ($NonInteractive -and (!$GameDirectory -or !$RuntimeDirectory)) {
    throw 'NonInteractive requires both GameDirectory and RuntimeDirectory.'
}
if (!$GameDirectory) { $GameDirectory = Select-Directory 'Select your game folder (contains CUSA03173)' }
if (!$RuntimeDirectory) { $RuntimeDirectory = Select-Directory 'Select the installed bloodborne_pc / bbport folder' }
if ($InstallPrerequisites) {
    $prereqArgs = @{ NonInteractive = $NonInteractive.IsPresent }
    & (Join-Path $PSScriptRoot 'src/scripts/install-prerequisites.ps1') @prereqArgs
    if (!$?) { throw 'Prerequisite installation failed.' }
}
if (Test-Path "$env:USERPROFILE/.cargo/bin/cargo.exe") { $env:PATH = "$env:USERPROFILE/.cargo/bin;" + $env:PATH }
$python = Get-Command python -ErrorAction SilentlyContinue
if (!$python) { throw 'Python 3.11+ is required. Run again with -InstallPrerequisites.' }
$arguments = @((Join-Path $PSScriptRoot 'src/scripts/toolkit.py'), 'setup',
    '--game', $GameDirectory, '--runtime', $RuntimeDirectory, '--mode', $Mode)
if ($ConfigureOnly) { $arguments += '--configure-only' }
if ($ToolCache) { $arguments += @('--tool-cache', $ToolCache) }
& $python.Source @arguments
if ($LASTEXITCODE -ne 0) { throw "Toolkit setup failed ($LASTEXITCODE). Fix the reported step and rerun the same command." }
