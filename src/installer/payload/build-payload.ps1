<#
Assembles payload/payload.zip, which build.rs embeds in the installer.

The archive holds payload.json and blobs/<sha256> (each distinct file once). payload.json lists:
  shared    the Mod Manager (bbmods/bbmod.manager/...), installed for every bbport version
  runtimes  one patched runtime per supported bbport version, with the SHA-256 of the stock
            bb-probe.exe(s) it replaces so the installer can tell which one a player has

Runtimes come from .local/runtime-stage/<id>/bin (built by `bb.ps1 build-runtime -Runtime <id>`);
the ids and stock hashes come from src/scripts/tools.lock.json. Nothing here reads or copies game
data, saves or settings.
#>
[CmdletBinding()]
param(
    [string]$StageRoot = (Join-Path $PSScriptRoot '../../../.local/runtime-stage'),
    [string]$ManagerDir = (Join-Path $PSScriptRoot '../../manager'),
    [string]$Lock = (Join-Path $PSScriptRoot '../../scripts/tools.lock.json'),
    # Only these runtime ids (default: every staged runtime that tools.lock.json knows).
    [string[]]$Runtimes,
    [switch]$SkipRuntime
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem

$dll = Join-Path $ManagerDir 'target/release/bbmod_manager.dll'
$toml = Join-Path $ManagerDir 'mod.toml'
foreach ($f in $dll, $toml) { if (!(Test-Path $f)) { throw "Missing $f. Build the manager first: cargo build --release in src/manager." } }

$blobs = [ordered]@{}   # sha256 -> source path
function Add-Blob([string]$path) {
    $sha = (Get-FileHash $path -Algorithm SHA256).Hash.ToLowerInvariant()
    if (!$blobs.Contains($sha)) { $blobs[$sha] = $path }
    $sha
}

$shared = @(
    [ordered]@{ path = 'bbmods/bbmod.manager/mod.toml'; sha256 = (Add-Blob $toml) }
    [ordered]@{ path = 'bbmods/bbmod.manager/bbmod_manager.dll'; sha256 = (Add-Blob $dll) }
)

$runtimeList = @()
if (!$SkipRuntime) {
    $specs = (Get-Content $Lock -Raw | ConvertFrom-Json).runtimes
    foreach ($spec in $specs) {
        if ($Runtimes -and $spec.id -notin $Runtimes) { continue }
        $bin = Join-Path $StageRoot "$($spec.id)/bin"
        if (!(Test-Path (Join-Path $bin 'bb-probe.exe'))) { Write-Warning "No staged runtime for $($spec.id) ($bin); it will not be supported by this installer."; continue }
        $files = @()
        foreach ($f in Get-ChildItem $bin -File | Where-Object { $_.Extension -in '.exe', '.dll' } | Sort-Object Name) {
            $files += [ordered]@{ path = "bin/$($f.Name)"; sha256 = (Add-Blob $f.FullName) }
        }
        $probe = ($files | Where-Object path -eq 'bin/bb-probe.exe').sha256
        $runtimeList += [ordered]@{ id = $spec.id; label = $spec.label; stock_probe_sha256 = @($spec.stock_probe_sha256); probe_sha256 = $probe; files = $files }
    }
    if (!$runtimeList) { throw 'No staged runtime found. Run .\bb.ps1 build-runtime -Runtime <id>, or pass -SkipRuntime for a manager-only payload.' }
}

$manifest = [ordered]@{ format = 2; version = '1'; shared = $shared; runtimes = $runtimeList }
$out = Join-Path $PSScriptRoot 'payload.zip'
Remove-Item $out -ErrorAction SilentlyContinue
$zip = [IO.Compression.ZipFile]::Open($out, 'Create')
try {
    $entry = $zip.CreateEntry('payload.json', 'Optimal')
    $w = New-Object IO.StreamWriter($entry.Open(), (New-Object Text.UTF8Encoding($false)))
    $w.Write(($manifest | ConvertTo-Json -Depth 8)); $w.Dispose()
    foreach ($sha in $blobs.Keys) {
        [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $blobs[$sha], "blobs/$sha", 'Optimal')
    }
} finally { $zip.Dispose() }
'{0} runtime(s) [{1}], {2} files, {3:N1} MiB -> {4}' -f $runtimeList.Count, (($runtimeList | ForEach-Object id) -join ', '), $blobs.Count, ((Get-Item $out).Length / 1MB), $out
