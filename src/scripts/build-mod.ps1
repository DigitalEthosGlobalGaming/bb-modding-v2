param([Parameter(Mandatory=$true)][string]$Project, [switch]$Test)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'dev-env.ps1')
$projectRoot = (Resolve-Path -LiteralPath $Project -ErrorAction Stop).Path
$script = Join-Path $projectRoot 'build.ps1'
if (Test-Path -LiteralPath $script) {
    $parameters = @{}
    $command = Get-Command -Name $script
    if ($command.Parameters.ContainsKey('NoDeploy')) { $parameters.NoDeploy = $true }
    if ($Test) { $parameters.Test = $true }
    Push-Location $projectRoot
    try { & $script @parameters } finally { Pop-Location }
} else {
    $manifest = Join-Path $projectRoot 'Cargo.toml'
    if (!(Test-Path -LiteralPath $manifest)) { throw 'Project needs Cargo.toml or build.ps1' }
    & cargo build --release --manifest-path $manifest
    if ($LASTEXITCODE) { throw 'Mod build failed' }
    if ($Test) {
        & cargo test --manifest-path $manifest
        if ($LASTEXITCODE) { throw 'Mod tests failed' }
    }
}

