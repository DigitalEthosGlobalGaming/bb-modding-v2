param([switch]$Native, [string]$Msys2Root = 'C:/msys64')
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
Push-Location $root
try {
    & python -m unittest discover -s src/tests/toolkit -v
    if ($LASTEXITCODE) { throw 'Python toolkit tests failed.' }
    $cargo = if (Get-Command cargo -ErrorAction SilentlyContinue) { 'cargo' } else { "$env:USERPROFILE/.cargo/bin/cargo.exe" }
    & $cargo test --workspace --locked
    if ($LASTEXITCODE) { throw 'Rust tests failed.' }
    if ($Native) {
        $env:PATH = "$Msys2Root/clang64/bin;" + $env:PATH
        & cmake -S . -B build/native -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release -DBBMOD_WITH_ZYDIS=ON "-DCMAKE_PREFIX_PATH=$Msys2Root/clang64"
        if ($LASTEXITCODE) { throw 'CMake configure failed.' }
        & cmake --build build/native --parallel 4
        if ($LASTEXITCODE) { throw 'Native build failed.' }
        & ctest --test-dir build/native --output-on-failure --timeout 300
        if ($LASTEXITCODE) { throw 'Native tests failed.' }
    }
    & python src/scripts/audit_publication.py
    if ($LASTEXITCODE) { throw 'Publication audit failed.' }
} finally { Pop-Location }
