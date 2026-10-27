# Post-build (testing): copies the built mod into the game folder.
#   <GameDir>\RDR_bhaptics.red                      always replaced
#   <GameDir>\RDR_bhaptics\bhaptics_library.dll     always replaced
#   <GameDir>\RDR_bhaptics\RDR_bhaptics.ini         only when missing: your AppId / ApiKey / test settings stay
# A copy that fails (the game still running and holding the files) is a warning, not a failed build.
param(
    [Parameter(Mandatory)] [string]$Red,
    [Parameter(Mandatory)] [string]$ProjectDir,
    [Parameter(Mandatory)] [string]$LibDir,
    [Parameter(Mandatory)] [string]$GameDir
)
$ErrorActionPreference = "Stop"

if (-not (Test-Path (Join-Path $GameDir "RDR.exe"))) {
    Write-Host "deploy_game: no RDR.exe in $GameDir, skipped"
    exit 0
}

$sub = Join-Path $GameDir "RDR_bhaptics"
New-Item -ItemType Directory -Path $sub -Force | Out-Null

try {
    Copy-Item $Red $GameDir -Force
    Copy-Item (Join-Path $LibDir "bhaptics_library.dll") $sub -Force
} catch {
    Write-Host "deploy_game: warning: copy failed (is the game running?): $($_.Exception.Message)"
    exit 0
}

$ini = Join-Path $sub "RDR_bhaptics.ini"
if (Test-Path $ini) {
    Write-Host "deploy_game: kept the existing RDR_bhaptics.ini"
} else {
    Copy-Item (Join-Path $ProjectDir "config\RDR_bhaptics.ini") $ini
    Write-Host "deploy_game: copied the default RDR_bhaptics.ini"
}
Write-Host "deploy_game: copied to $GameDir"
exit 0
