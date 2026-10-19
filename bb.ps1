param([Parameter(ValueFromRemainingArguments=$true)][string[]]$Arguments)
$ErrorActionPreference = 'Stop'
& python (Join-Path $PSScriptRoot 'src/scripts/toolkit.py') @Arguments
if ($LASTEXITCODE -ne 0) { throw "Toolkit command failed ($LASTEXITCODE)" }
