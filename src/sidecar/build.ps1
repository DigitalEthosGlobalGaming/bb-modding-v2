<#
Builds dist/dxgi.dll: the sidecar loader (see sidecar.c). Spike: nothing is installed anywhere.
Needs MSYS2 CLANG64 with clang and zydis (the runtime build installs both).
#>
[CmdletBinding()]
param([string]$Msys2Root = 'C:/msys64')
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$env:PATH = "$Msys2Root/clang64/bin;" + $env:PATH
$out = Join-Path $PSScriptRoot 'dist'
New-Item -ItemType Directory -Force $out | Out-Null
$sources = @((Join-Path $PSScriptRoot 'sidecar.c')) + (Get-ChildItem (Join-Path $root 'src/host/src') -Filter *.c | ForEach-Object FullName)
$args = @('-std=gnu11', '-O2', '-shared', '-fms-extensions', '-DBBM_WITH_ZYDIS=1', '-D_WIN32_WINNT=0x0A00',
    '-I', (Join-Path $root 'src/host/include'), '-I', (Join-Path $root 'src/host/src'), '-I', (Join-Path $root 'sdk/include'),
    '-o', (Join-Path $out 'dxgi.dll')) + $sources + @('-lZydis', '-lZycore', '-lws2_32', '-luser32')
& clang @args
if ($LASTEXITCODE -ne 0) { throw 'sidecar build failed' }
Get-Item (Join-Path $out 'dxgi.dll') | ForEach-Object { '{0} ({1:N0} KiB)' -f $_.FullName, ($_.Length / 1KB) }
