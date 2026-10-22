#requires -Version 7.0
param([Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$library = Join-Path $workspace 'tools/Smithbox/2.2.6/Andre.SoulsFormats.dll'
[void][System.Reflection.Assembly]::LoadFrom($library)
$mapFolder = Join-Path $workspace 'game-unpacked/raw/dvdroot_ps4/map/mapstudio'
$sources = @()
$placements = @(
    foreach ($file in (Get-ChildItem -LiteralPath $mapFolder -Filter '*.msb' | Sort-Object Name)) {
        $sources += @{ path = $file.Name; sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
        $map = [SoulsFormats.MSBB]::Read($file.FullName)
        foreach ($part in $map.Parts.Enemies) {
            [ordered]@{ map = $file.BaseName; part = $part.Name; npc = $part.NPCParamID;
                think = $part.ThinkParamID; model = $part.ModelName; entity = $part.EntityID }
        }
    }
)
New-Item -ItemType Directory -Force (Split-Path $Output) | Out-Null
@{ sources = $sources; placements = $placements; reader = 'Smithbox 2.2.6 / Andre.SoulsFormats MSBB';
   reader_sha256 = (Get-FileHash -LiteralPath $library -Algorithm SHA256).Hash.ToLowerInvariant();
   description = 'Baseline MSB Enemy placements only; excludes DummyEnemy. Not a live actor/script binding.'
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $Output -Encoding utf8
Write-Host "$($placements.Count) enemy placements from $($sources.Count) maps exported."
