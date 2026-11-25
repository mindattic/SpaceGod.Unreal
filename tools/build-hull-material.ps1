[CmdletBinding()]
param(
  [string]$EngineVersion = '5.8'
)

<#
.SYNOPSIS
Build /Game/SpaceGod/Materials/M_HullMaster and M_HullShield, and an MI_<TAG>_<Atlas>_<Colour>_Hull beside each ship
pack's _Lit instance (US-T849), by running tools/hull_material.py headless (UnrealEditor-Cmd -run=pythonscript). The
packs' textures come from tools/import-usc.ps1 / import-ssc.ps1, so run one of those first. Close any editor that has
the project open first.
#>

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/engine.ps1"
$repo = Split-Path -Parent $PSScriptRoot
$project = Join-Path $repo 'Cpp\SpaceGod.uproject'
$engine = Find-UnrealEngine $EngineVersion
$editor = Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$script = (Join-Path $PSScriptRoot 'hull_material.py') -replace '\\', '/'
& $editor $project -run=pythonscript "-script=$script" -unattended -nop4 -nosplash -stdout -FullStdOutLogOutput |
  Where-Object { $_ -match 'Hull:|Error' }
if ($LASTEXITCODE) { throw "Hull material build failed (exit $LASTEXITCODE)." }
