[CmdletBinding()]
param(
  # Blender 3.3 or later; the newest under Program Files\Blender Foundation when not given.
  [string]$Blender = ''
)

<#
.SYNOPSIS
Write Data/ModularParts.json (US-T763): every modular part of the imported third-party kits (Ultimate Spaceships
Creator, Star Sparrow, Space Station Creator) by role, with its bounds, attach sockets and style tags, read from the
packs' FBX files under Cpp/SourceArt/ThirdParty by tools/index_kit_parts.py in Blender. The game's console command
sg.IndexKitParts does the same. The index holds names and numbers only and may be committed; the packs may not.
#>

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$source = Join-Path $repo 'Cpp\SourceArt\ThirdParty'
$out = Join-Path $repo 'Data\ModularParts.json'
if (-not (Test-Path -LiteralPath $source)) { throw "No kit is imported here ($source); see docs/ThirdPartyArt.md." }
if (-not $Blender) {
  $Blender = Get-ChildItem 'C:\Program Files\Blender Foundation' -Directory -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending | ForEach-Object { Join-Path $_.FullName 'blender.exe' } |
    Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if (-not $Blender -or -not (Test-Path -LiteralPath $Blender)) { throw 'Blender not found (pass -Blender <path to blender.exe>, 3.3 or later).' }
# Blender writes harmless notices to stderr, which Windows PowerShell would otherwise turn into a terminating error.
$ErrorActionPreference = 'Continue'
& $Blender -b --factory-startup --python (Join-Path $PSScriptRoot 'index_kit_parts.py') -- $source $out 2>&1 |
  ForEach-Object { "$_" } | Where-Object { $_ -match '^KitParts' }
if ($LASTEXITCODE) { throw "Indexing failed (exit $LASTEXITCODE)." }
