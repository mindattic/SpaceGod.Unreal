[CmdletBinding()]
param(
  # Fetch and unpack everything again, even when the files on disk already match.
  [switch]$Force
)

<#
.SYNOPSIS
Fetch the local text-to-speech engine for spoken comms (US-T574): Piper and its voice, into Vendor/Piper/ (gitignored).

.DESCRIPTION
Pinned downloads, each checked against its SHA256:
 - Piper 2023.11.14-2, the last MIT release (rhasspy/piper; not its GPL successor piper1-gpl): piper_windows_amd64.zip,
   unpacked to Vendor/Piper/piper/ (piper.exe, its DLLs and espeak-ng-data).
 - The voice en_US-libritts_r-medium (CC BY 4.0, 904 speakers) from huggingface.co/rhasspy/piper-voices at a pinned
   revision: its .onnx and .onnx.json in Vendor/Piper/voices/.
Idempotent: a file already present with the right hash is kept, and the zip is unpacked again only when its stamp
(Vendor/Piper/piper/.version) does not name the pinned hash. Licences and refreshing: Vendor/README.md.

.EXAMPLE
  ./tools/fetch-piper.ps1
  ./tools/fetch-piper.ps1 -Force
#>

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$repo = Split-Path -Parent $PSScriptRoot
$root = Join-Path $repo 'Vendor\Piper'
$piperDir = Join-Path $root 'piper'
$voicesDir = Join-Path $root 'voices'
$downloads = Join-Path $root '.download'

$piperTag = '2023.11.14-2'
$voicesRevision = 'e5349433ed883a32f893a434dc43a6c66b95ef7c'
$voiceBase = "https://huggingface.co/rhasspy/piper-voices/resolve/$voicesRevision/en/en_US/libritts_r/medium"
$zip = @{
  Url = "https://github.com/rhasspy/piper/releases/download/$piperTag/piper_windows_amd64.zip"
  Path = Join-Path $downloads 'piper_windows_amd64.zip'
  Sha256 = 'F3C58906402B24F3A96D92145F58ACBA6D86C9B5DB896D207F78DC80811EFCEA'
}
$voices = @(
  @{
    Url = "$voiceBase/en_US-libritts_r-medium.onnx"
    Path = Join-Path $voicesDir 'en_US-libritts_r-medium.onnx'
    Sha256 = '10BB85E071D616FCF4071F369F1799D0491492AB3C5D552EC19FB548FAC13195'
  },
  @{
    Url = "$voiceBase/en_US-libritts_r-medium.onnx.json"
    Path = Join-Path $voicesDir 'en_US-libritts_r-medium.onnx.json'
    Sha256 = 'B471DC60D2D8335E819C393D196D6FBF792817F40051257B269878505BC9AFB3'
  }
)

function Test-Sha256([string]$Path, [string]$Sha256) {
  (Test-Path -LiteralPath $Path) -and ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -eq $Sha256)
}

# Downloads to <path>.part and moves it into place only once the hash matches, so a broken download never stays.
function Get-PinnedFile($File) {
  if (-not $Force -and (Test-Sha256 $File.Path $File.Sha256)) {
    Write-Host "Up to date: $($File.Path)"
    return
  }
  New-Item -ItemType Directory -Force (Split-Path -Parent $File.Path) | Out-Null
  $part = "$($File.Path).part"
  Write-Host "Fetching $($File.Url)"
  Invoke-WebRequest -Uri $File.Url -OutFile $part -UseBasicParsing
  $actual = (Get-FileHash -LiteralPath $part -Algorithm SHA256).Hash
  if ($actual -ne $File.Sha256) {
    Remove-Item -LiteralPath $part -Force
    throw "SHA256 mismatch for $($File.Url): expected $($File.Sha256), got $actual."
  }
  Move-Item -LiteralPath $part -Destination $File.Path -Force
}

# Piper: the zip holds one top-level piper/ folder, unpacked over Vendor/Piper/piper/.
$stamp = Join-Path $piperDir '.version'
$unpacked = (Test-Path -LiteralPath (Join-Path $piperDir 'piper.exe')) -and (Test-Path -LiteralPath $stamp) -and
  ((Get-Content -Raw -LiteralPath $stamp).Trim() -eq "$piperTag $($zip.Sha256)")
if ($Force -or -not $unpacked) {
  Get-PinnedFile $zip
  if (Test-Path -LiteralPath $piperDir) { Remove-Item -LiteralPath $piperDir -Recurse -Force }
  Expand-Archive -LiteralPath $zip.Path -DestinationPath $root -Force
  if (-not (Test-Path -LiteralPath (Join-Path $piperDir 'piper.exe'))) { throw "The Piper zip unpacked no piper.exe in $piperDir." }
  [IO.File]::WriteAllText($stamp, "$piperTag $($zip.Sha256)")
  Write-Host "Unpacked Piper $piperTag into $piperDir"
} else {
  Write-Host "Up to date: Piper $piperTag in $piperDir"
}

foreach ($voice in $voices) { Get-PinnedFile $voice }

Write-Host "Piper ready: $(Join-Path $piperDir 'piper.exe') with $($voices[0].Path)"
