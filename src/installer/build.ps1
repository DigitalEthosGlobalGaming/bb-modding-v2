<#
Builds dist/bb-modding-installer.exe: the mod manager, the payload, then the installer.
Use -SkipRuntime when no staged runtime exists (the result then installs only the manager).
Nothing is deployed to a game or port.
#>
[CmdletBinding()]
param([switch]$SkipRuntime, [switch]$Test, [string]$RuntimeStage)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
function Run([string]$dir, [scriptblock]$cmd) { Push-Location $dir; try { & $cmd; if ($LASTEXITCODE -ne 0) { throw "Failed in $dir" } } finally { Pop-Location } }

Run (Join-Path $here '../manager') { cargo build --release --locked 2>&1 | Write-Host }
if ($Test) { Run (Join-Path $here '../manager') { cargo test --release 2>&1 | Write-Host } }

$args = @{ SkipRuntime = $SkipRuntime.IsPresent }
if ($RuntimeStage) { $args.RuntimeStage = $RuntimeStage }
& (Join-Path $here 'payload/build-payload.ps1') @args

if ($Test) { Run $here { cargo test --release 2>&1 | Write-Host } }
Run $here { cargo build --release --locked 2>&1 | Write-Host }

$dist = Join-Path $here 'dist'
New-Item -ItemType Directory -Force $dist | Out-Null
$exe = Join-Path $dist 'bb-modding-installer.exe'
Copy-Item (Join-Path $here 'target/release/bb-modding-installer.exe') $exe -Force
$hash = (Get-FileHash $exe -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  bb-modding-installer.exe" | Set-Content -Encoding ascii "$exe.sha256"
'{0}  ({1:N1} MiB)' -f $exe, ((Get-Item $exe).Length / 1MB)
