#Requires -Version 5.1
<#
.SYNOPSIS
    SpaceGod's run menu: 1) Redeploy  2) Run Debug  3) Run Release  (the MindAttic standard, MAT-T279).

.DESCRIPTION
    A thin wrapper over the shared deploy-lib.ps1 (Invoke-AppRun, Publish-Unreal; US-T873). run.bat beside it
    (written by deploy-lib.ps1's Write-RunBat) opens the same menu on a double-click.
      1) Redeploy     tools\package.ps1 packages DebugGame and Shipping into Builds\Cpp\Windows, each copied to
                      R:\MindAttic\Apps\SpaceGod.Unreal\{Debug,Release}
      2) Run Debug    R:\...\Debug\SpaceGod.exe in this console; extra arguments pass through
      3) Run Release  R:\...\Release\SpaceGod.exe, started the way a double-click would start it

    SpaceGod's own differences, in the override block below:
      - tools\package.ps1 does the packaging beyond UAT: the exe renamed SpaceGod.exe, the MindAttic.Media.Capture
        agent, Data/Lore's Prose texts, Vehicles.json and Piper with its voices staged beside Engine/.
      - Every launch records through MindAttic.Media.Capture (-capture video hq, US-T74; docs/CAPTURE.md).
      - After the Release deploy, the Desktop shortcut "SpaceGod" opens run.bat, with the Release exe's icon.

.EXAMPLE
    .\run.ps1                    the menu
    .\run.ps1 -Redeploy          package and deploy both configs
    .\run.ps1 -Redeploy -RunRelease
    .\run.ps1 -Redeploy -DryRun
#>
param(
    [switch]$Redeploy,
    [switch]$RunDebug,
    [switch]$RunRelease,
    [switch]$DryRun,
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$AppArgs = @()
)
$ErrorActionPreference = 'Stop'

$repoRoot = $PSScriptRoot
$captureArgs = @('-capture', 'video', 'hq')
$Override = @{
    Exe         = 'SpaceGod.exe'
    Plan        = @{ Kind = 'unreal' }
    DebugArgs   = $captureArgs
    ReleaseArgs = $captureArgs
    PostBuild   = {
        param($Config, $Out)
        if ($Config -ne 'Release') { return }
        # GetFolderPath, not $HOME\Desktop: OneDrive may have moved the Desktop folder.
        $shortcut = Join-Path ([Environment]::GetFolderPath('Desktop')) 'SpaceGod.lnk'
        $link = (New-Object -ComObject WScript.Shell).CreateShortcut($shortcut)
        $link.TargetPath       = Join-Path $repoRoot 'run.bat'
        $link.WorkingDirectory = $repoRoot
        $link.IconLocation     = "$(Join-Path $Out 'SpaceGod.exe'),0"
        $link.Description      = 'SpaceGod: 1) Redeploy  2) Run Debug  3) Run Release'
        $link.Save()
        Write-Host "    Shortcut -> $shortcut" -ForegroundColor Gray
    }.GetNewClosure()
}

function Find-DeployLib {
    if ($env:MINDATTIC_DEPLOY_LIB -and (Test-Path -LiteralPath $env:MINDATTIC_DEPLOY_LIB)) { return $env:MINDATTIC_DEPLOY_LIB }
    $roots = @()
    $d = $PSScriptRoot
    while ($d) { $roots += $d; $d = Split-Path -Parent $d }
    $roots += 'D:\Projects\MindAttic'
    foreach ($r in $roots) {
        foreach ($rel in 'MindAttic.Agency\shared\deploy-lib.ps1', 'mindattic-agent-standard\shared\deploy-lib.ps1') {
            $p = Join-Path $r $rel
            if (Test-Path -LiteralPath $p) { return $p }
        }
    }
    throw 'deploy-lib.ps1 not found: clone MindAttic.Agency beside this repo, or set MINDATTIC_DEPLOY_LIB to its path.'
}

. (Find-DeployLib)

Invoke-AppRun -Repo $PSScriptRoot -Redeploy:$Redeploy -RunDebug:$RunDebug -RunRelease:$RunRelease -DryRun:$DryRun `
    -AppArgs $AppArgs -Override $Override
