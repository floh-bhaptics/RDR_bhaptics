# Post-build: packs the plugin into <OutDir>\RDR_bhaptics.zip, laid out to unpack into the game folder:
#   RDR_bhaptics.red
#   RDR_bhaptics\RDR_bhaptics.ini
#   RDR_bhaptics\bhaptics_library.dll
param(
    [Parameter(Mandatory)] [string]$Red,         # the built .red ($(TargetPath))
    [Parameter(Mandatory)] [string]$ProjectDir,
    [Parameter(Mandatory)] [string]$OutDir,
    [Parameter(Mandatory)] [string]$LibDir       # holds bhaptics_library.dll
)
$ErrorActionPreference = "Stop"

$dll = Join-Path $LibDir "bhaptics_library.dll"
$ini = Join-Path $ProjectDir "config\RDR_bhaptics.ini"
foreach ($f in @($Red, $dll, $ini)) {
    if (-not (Test-Path $f)) { Write-Error "postbuild_zip: missing $f"; exit 1 }
}

$stage = Join-Path $OutDir "package"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
$sub = Join-Path $stage "RDR_bhaptics"
New-Item -ItemType Directory -Path $sub -Force | Out-Null

Copy-Item $Red $stage
Copy-Item $ini $sub
Copy-Item $dll $sub

$zip = Join-Path $OutDir "RDR_bhaptics.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip
Write-Host "postbuild_zip: $zip"
exit 0
