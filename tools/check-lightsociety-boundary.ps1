[CmdletBinding()]
param()

<#
.SYNOPSIS
Checks that Light Society (Cpp/Source/SpaceGodCore/LightSociety) stays a std-only library (US-T964).

.DESCRIPTION
Every file under LightSociety/ may include only standard library headers (<name>, no extension or path) and
"LightSociety/..." headers, and may not use SpaceGod's export macro or the Cs layer. Prints each offending line and
exits 1 when there is one, else exits 0.

.EXAMPLE
tools/check-lightsociety-boundary.ps1
#>

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$root = Join-Path $repo 'Cpp\Source\SpaceGodCore\LightSociety'
if (-not (Test-Path -LiteralPath $root)) { Write-Host "No $root."; exit 1 }

$bad = 0
$files = @(Get-ChildItem -LiteralPath $root -Recurse -File)
foreach ($f in $files) {
  $rel = $f.FullName.Substring($repo.Length).TrimStart('\')
  $n = 0
  foreach ($line in [IO.File]::ReadAllLines($f.FullName)) {
    $n++
    $why = $null
    if ($line -match '^\s*#\s*include\b') {
      if ($line -notmatch '^\s*#\s*include\s*(<[a-z_]+>|"LightSociety/[^"]+")\s*(//.*)?$') { $why = 'include outside the boundary' }
    } elseif ($line -match '\bSPACEGODCORE_API\b') { $why = 'export macro'
    } elseif ($line -match '\bcs::') { $why = 'Cs layer type' }
    if ($why) {
      Write-Host "${rel}:${n}: ${why}: $($line.Trim())" -ForegroundColor Red
      $bad++
    }
  }
}

if ($bad -gt 0) { Write-Host "Light Society boundary: $bad violation(s)." -ForegroundColor Red; exit 1 }
Write-Host "Light Society boundary: ok ($($files.Count) file(s))."
exit 0
