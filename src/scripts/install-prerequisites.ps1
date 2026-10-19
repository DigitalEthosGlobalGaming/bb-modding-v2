param([switch]$NonInteractive, [switch]$RuntimeBuild)
$ErrorActionPreference = 'Stop'
if (Test-Path "$env:USERPROFILE/.cargo/bin/cargo.exe") { $env:PATH = "$env:USERPROFILE/.cargo/bin;" + $env:PATH }
$packages = @(
    @{ Id = 'Python.Python.3.11'; Label = 'Python 3.11'; Test = 'python' },
    @{ Id = 'Git.Git'; Label = 'Git'; Test = 'git' },
    @{ Id = 'Rustlang.Rustup'; Label = 'Rust MSVC toolchain'; Test = 'cargo' },
    @{ Id = 'Microsoft.DotNet.SDK.9'; Label = '.NET SDK 9'; Test = 'dotnet' }
)
foreach ($package in $packages) {
    if (Get-Command $package.Test -ErrorAction SilentlyContinue) { continue }
    Write-Host "Required: $($package.Label) via winget ($($package.Id)). System installers may request elevation."
    if (!$NonInteractive -and (Read-Host 'Install? [y/N]') -notmatch '^[yY]') { throw 'Prerequisite declined.' }
    & winget install --exact --id $package.Id --source winget --accept-source-agreements --accept-package-agreements
    if ($LASTEXITCODE -ne 0) { throw "Installation failed: $($package.Id)" }
}
Write-Host 'Rust DLL builds also require Visual Studio C++ tools and the Windows SDK.'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$cpp = if (Test-Path $vswhere) { & $vswhere -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath }
if (!$cpp) {
    Write-Host 'Installing Visual Studio 2022 Build Tools with the C++ workload; elevation may be requested.'
    if (!$NonInteractive -and (Read-Host 'Install C++ Build Tools? [y/N]') -notmatch '^[yY]') { throw 'C++ tools declined.' }
    & winget install --exact --id Microsoft.VisualStudio.2022.BuildTools --source winget --accept-source-agreements --accept-package-agreements --override '--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended'
    if ($LASTEXITCODE -notin @(0,3010)) { throw 'C++ tool installation failed.' }
}
if ($RuntimeBuild -and !(Test-Path 'C:/msys64/usr/bin/bash.exe')) {
    Write-Host 'Runtime compilation requires MSYS2 CLANG64 and its native libraries.'
    if (!$NonInteractive -and (Read-Host 'Install MSYS2? [y/N]') -notmatch '^[yY]') { throw 'MSYS2 declined.' }
    & winget install --exact --id MSYS2.MSYS2 --source winget --accept-source-agreements --accept-package-agreements
    if ($LASTEXITCODE -ne 0) { throw 'MSYS2 installation failed.' }
}
$env:PATH = [Environment]::GetEnvironmentVariable('PATH','Machine') + ';' + [Environment]::GetEnvironmentVariable('PATH','User')
if (Test-Path "$env:USERPROFILE/.cargo/bin") { $env:PATH += ";$env:USERPROFILE/.cargo/bin" }
Write-Host 'Prerequisite step finished. Restart PowerShell if a newly installed command is not yet available.'
