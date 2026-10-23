<#
Assembles payload/payload.zip, which build.rs embeds in the installer.

Layout of the archive (paths are relative to the bbport folder):
  bin/*.exe, bin/*.dll                      the patched runtime and its DLLs (from -RuntimeStage)
  bbmods/bbmod.manager/mod.toml + .dll      the mod manager (from -ManagerDir)

Nothing here reads or copies game data, saves or settings.
#>
[CmdletBinding()]
param(
    [string]$RuntimeStage = (Join-Path $PSScriptRoot '../../../.local/runtime-stage'),
    [string]$ManagerDir = (Join-Path $PSScriptRoot '../../manager'),
    [switch]$SkipRuntime
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem

$dll = Join-Path $ManagerDir 'target/release/bbmod_manager.dll'
$toml = Join-Path $ManagerDir 'mod.toml'
foreach ($f in $dll, $toml) { if (!(Test-Path $f)) { throw "Missing $f. Build the manager first: cargo build --release in src/manager." } }

$files = [ordered]@{
    'bbmods/bbmod.manager/mod.toml' = $toml
    'bbmods/bbmod.manager/bbmod_manager.dll' = $dll
}
if (!$SkipRuntime) {
    $bin = Join-Path $RuntimeStage 'bin'
    $runtime = @(Get-ChildItem $bin -File -ErrorAction SilentlyContinue | Where-Object { $_.Extension -in '.exe', '.dll' })
    if (!($runtime | Where-Object Name -eq 'bb-probe.exe')) { throw "No staged bb-probe.exe in $bin. Run .\bb.ps1 build-runtime, or pass -SkipRuntime for a manager-only payload." }
    foreach ($r in $runtime) { $files["bin/$($r.Name)"] = $r.FullName }
}

$out = Join-Path $PSScriptRoot 'payload.zip'
Remove-Item $out -ErrorAction SilentlyContinue
$zip = [IO.Compression.ZipFile]::Open($out, 'Create')
try {
    foreach ($name in $files.Keys) {
        [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $files[$name], $name, 'Optimal')
    }
} finally { $zip.Dispose() }
'{0} files, {1:N1} MiB -> {2}' -f $files.Count, ((Get-Item $out).Length / 1MB), $out
