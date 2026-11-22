[CmdletBinding()]
param(
  # The Meshy export folder: one .fbx and its *_texture/_metallic/_roughness/_normal.png.
  [string]$Source = (Join-Path $env:USERPROFILE 'Downloads\Meshy_AI_Midnight_Manalith_20261122_texture_fbx\Meshy_AI_Midnight_Manalith_20261122_texture_fbx'),
  # Blender 3.3 or later; the newest under Program Files\Blender Foundation when not given.
  [string]$Blender = '',
  [string]$EngineVersion = '5.8',
  # Faces the mesh is decimated to.
  [int]$Faces = 60000,
  # Re-import into Unreal only, from the pieces already prepared.
  [switch]$SkipPrepare
)

<#
.SYNOPSIS
Rebuild /Game/SpaceGod/Manalith, the Manalith of Nightglass (US-T458, docs/MANALITH_VISUAL_SPEC.md), from the owner's
Meshy export: Blender decimates and centres the mesh and sizes the maps (tools/manalith_prepare.py) into Cpp/SourceArt/Manalith/,
then tools/import_manalith.py imports them headless and builds M_ManalithLit and M_ManalithShard. The imported assets are committed;
the 58 MB source and the prepared pieces are not. Launch from PowerShell (Git Bash mangles /Game paths). Close any editor
that has the project open first.
#>

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/engine.ps1"
$repo = Split-Path -Parent $PSScriptRoot
$project = Join-Path $repo 'Cpp\SpaceGod.uproject'
$pieces = Join-Path $repo 'Cpp\SourceArt\Manalith'

if (-not $SkipPrepare) {
  if (-not (Test-Path -LiteralPath $Source)) { throw "Meshy export not found: $Source (pass -Source <folder>)." }
  if (-not $Blender) {
    $Blender = Get-ChildItem 'C:\Program Files\Blender Foundation' -Directory -ErrorAction SilentlyContinue |
      Sort-Object Name -Descending | ForEach-Object { Join-Path $_.FullName 'blender.exe' } |
      Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
  }
  if (-not $Blender -or -not (Test-Path -LiteralPath $Blender)) { throw 'Blender not found (pass -Blender <path to blender.exe>, 3.3 or later).' }
  $ErrorActionPreference = 'Continue'
  & $Blender -b --factory-startup --python (Join-Path $PSScriptRoot 'manalith_prepare.py') -- $Source $pieces $Faces 2>&1 |
    ForEach-Object { "$_" } | Where-Object { $_ -match '^MANALITH' }
  $ErrorActionPreference = 'Stop'
}
if (-not (Test-Path -LiteralPath (Join-Path $pieces 'manalith-satellite-00.fbx'))) { throw "No prepared model in $pieces." }

$engine = Find-UnrealEngine $EngineVersion
$editor = Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$script = (Join-Path $PSScriptRoot 'import_manalith.py') -replace '\\', '/'
$env:MANALITH_PIECES = $pieces
& $editor $project -run=pythonscript "-script=$script" -unattended -nop4 -nosplash -stdout -FullStdOutLogOutput |
  Where-Object { $_ -match 'MANALITH:|Error' }
if ($LASTEXITCODE) { throw "Manalith import failed (exit $LASTEXITCODE)." }
