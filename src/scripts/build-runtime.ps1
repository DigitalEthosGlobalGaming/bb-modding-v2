[CmdletBinding()]
param([string]$Msys2Root = 'C:/msys64', [switch]$InstallDependencies)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$bash = Join-Path $Msys2Root 'usr/bin/bash.exe'
if (!(Test-Path $bash)) { throw 'Install MSYS2 first: src/scripts/install-prerequisites.ps1 -RuntimeBuild' }
if ($InstallDependencies) {
    $env:MSYSTEM = 'CLANG64'
    & $bash -lc 'pacman -S --needed --noconfirm git patch mingw-w64-clang-x86_64-toolchain mingw-w64-clang-x86_64-cmake mingw-w64-clang-x86_64-ninja mingw-w64-clang-x86_64-pkgconf mingw-w64-clang-x86_64-python mingw-w64-clang-x86_64-vulkan-headers mingw-w64-clang-x86_64-vulkan-loader mingw-w64-clang-x86_64-vulkan-memory-allocator mingw-w64-clang-x86_64-glslang mingw-w64-clang-x86_64-sdl3 mingw-w64-clang-x86_64-fmt mingw-w64-clang-x86_64-boost mingw-w64-clang-x86_64-robin-map mingw-w64-clang-x86_64-xxhash mingw-w64-clang-x86_64-ffmpeg mingw-w64-clang-x86_64-zydis mingw-w64-clang-x86_64-spirv-tools mingw-w64-clang-x86_64-spirv-headers mingw-w64-clang-x86_64-spirv-cross mingw-w64-clang-x86_64-protobuf mingw-w64-clang-x86_64-openssl mingw-w64-clang-x86_64-zlib mingw-w64-clang-x86_64-miniupnpc mingw-w64-clang-x86_64-nlohmann-json'
    if ($LASTEXITCODE -ne 0) { throw 'Native dependency installation failed. Complete any MSYS2 core update first, then rerun.' }
}
& python (Join-Path $PSScriptRoot 'prepare_runtime.py')
if ($LASTEXITCODE -ne 0) { throw 'Runtime source preparation failed.' }
$env:MSYSTEM = 'CLANG64'
$env:BB_TOOLKIT_ROOT = $root.Replace('\','/')
& $bash -lc 'cd "$(cygpath -u "$BB_TOOLKIT_ROOT")/tools/bloodborne_pc" && git -c core.autocrlf=false submodule update --init --recursive && bash build.sh'
if ($LASTEXITCODE -ne 0) { throw 'Runtime build failed. Installed game was not modified.' }
# Stage the executable and its CLANG64 dependencies, without replacing launcher/settings/saves.
& $bash -lc 'cd "$(cygpath -u "$BB_TOOLKIT_ROOT")/tools/bloodborne_pc" && mkdir -p ../../.local/runtime-stage/bin && cp out/bb-probe.exe ../../.local/runtime-stage/bin/ && ldd out/bb-probe.exe | awk ''/\/clang64\/bin\// {print $3}'' | sort -u | while IFS= read -r dll; do cp "$dll" ../../.local/runtime-stage/bin/; done'
if ($LASTEXITCODE -ne 0) { throw 'Runtime staging failed.' }
& python (Join-Path $PSScriptRoot 'stage_runtime.py')
if ($LASTEXITCODE -ne 0) { throw 'Runtime manifest failed.' }
Write-Host 'Built separately. Inspect .local/runtime-stage, then run .\bb.ps1 deploy-runtime when ready.'
