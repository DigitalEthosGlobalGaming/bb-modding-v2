# Dot-source from a retained project. Existing toolchain choices take precedence.
$toolkit = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (!$env:WINPYTHON) { $env:WINPYTHON = (Get-Command python -ErrorAction Stop).Source }
$toolPaths = @((Join-Path $env:USERPROFILE '.cargo/bin'))
if ($env:MSYS2_ROOT) { $toolPaths += Join-Path $env:MSYS2_ROOT 'clang64/bin' }
elseif (Test-Path -LiteralPath 'C:/msys64/clang64/bin') { $toolPaths += 'C:/msys64/clang64/bin' }
$env:PATH = (($toolPaths | Where-Object { Test-Path -LiteralPath $_ }) -join ';') + ';' + $env:PATH
if (!$env:BBPORT_SOURCE) {
    $env:BBPORT_SOURCE = Join-Path $toolkit 'tools/bloodborne_pc'
    $stamp = Join-Path $toolkit '.local/runtime-source.json'
    if (Test-Path -LiteralPath $stamp) {
        $state = Get-Content -LiteralPath $stamp -Raw | ConvertFrom-Json
        if ($state.source) { $env:BBPORT_SOURCE = $state.source }
    }
}
