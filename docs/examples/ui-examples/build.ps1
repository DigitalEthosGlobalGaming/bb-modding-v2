param([switch]$Test)
$ErrorActionPreference = 'Stop'
$toolkitRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
. (Join-Path $toolkitRoot 'src/scripts/dev-env.ps1')
Push-Location $toolkitRoot
try {
    & cargo build --release --locked -p ui-examples
    if ($LASTEXITCODE) { throw 'UI-Examples-Mod build failed' }
    $metadata = & cargo metadata --format-version 1 --no-deps --locked | ConvertFrom-Json
    if ($LASTEXITCODE) { throw 'Cannot resolve Cargo target directory' }
    $releaseRoot = Join-Path $metadata.target_directory 'release'
    if ($Test) {
        & cargo test --locked -p ui-examples
        if ($LASTEXITCODE) { throw 'UI-Examples-Mod Rust tests failed' }
        & cargo build --release --locked -p input-counter
        if ($LASTEXITCODE) { throw 'Native fixture build failed' }
        $nativeRoot = Join-Path $toolkitRoot '.local/ui-examples-native'
        $msysRoot = if ($env:MSYS2_ROOT) { $env:MSYS2_ROOT } else { 'C:/msys64' }
        & cmake -S $toolkitRoot -B $nativeRoot -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release -DBBMOD_WITH_ZYDIS=ON "-DCMAKE_PREFIX_PATH=$msysRoot/clang64" "-DBBMOD_EXAMPLES_DIR=$($releaseRoot.Replace('\','/'))"
        if ($LASTEXITCODE) { throw 'Native fixture configure failed' }
        & cmake --build $nativeRoot --target test_native_ui_examples --parallel 4
        if ($LASTEXITCODE) { throw 'Native fixture build failed' }
        & ctest --test-dir $nativeRoot -R '^native_ui_examples$' --output-on-failure --timeout 120
        if ($LASTEXITCODE) { throw 'Actual DLL lifecycle/control tests failed' }
    }
    & python src/scripts/audit_publication.py
    if ($LASTEXITCODE) { throw 'Publication audit failed' }
    $packageRoot = Join-Path $toolkitRoot 'dist/example.ui-examples'
    New-Item -ItemType Directory -Force -Path $packageRoot | Out-Null
    Copy-Item -LiteralPath (Join-Path $releaseRoot 'ui_examples.dll') -Destination $packageRoot
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'mod.toml') -Destination $packageRoot
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'README.md') -Destination $packageRoot
    $packageFiles = @{}
    foreach ($name in @('ui_examples.dll', 'mod.toml', 'README.md')) {
        $packageFiles[$name] = (Get-FileHash -LiteralPath (Join-Path $packageRoot $name) -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    @{ schema_version = 1; mod_id = 'example.ui-examples'; stage = 'native-menu-baseline';
       automated_tests_passed = [bool]$Test; native_rendering_verified = $false;
       full_screen_implemented = $false; hud_mounting_implemented = $false;
       files = $packageFiles } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $packageRoot 'build-record.json') -Encoding utf8
    Write-Host "Packaged UI-Examples-Mod baseline: $packageRoot"
} finally { Pop-Location }
