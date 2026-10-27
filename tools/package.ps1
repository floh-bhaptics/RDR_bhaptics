# Builds the release zip: dist\RDR_bhaptics.zip, to unpack into the game folder.
#   RDR_bhaptics.red
#   RDR_bhaptics\RDR_bhaptics.ini
#   RDR_bhaptics\bhaptics_library.dll
param(
    [string]$BuildDir = "build",
    [string]$Config = "Release"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

cmake -S . -B $BuildDir -A x64
cmake --build $BuildDir --config $Config

$red = Join-Path $BuildDir "$Config\RDR_bhaptics.red"
if (-not (Test-Path $red)) { throw "build output not found: $red" }

$stage = Join-Path "dist" "stage"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Path (Join-Path $stage "RDR_bhaptics") | Out-Null

Copy-Item $red $stage
Copy-Item "config\RDR_bhaptics.ini" (Join-Path $stage "RDR_bhaptics")
Copy-Item "extern\tact-cpp2\sdk\lib\x64\bhaptics_library.dll" (Join-Path $stage "RDR_bhaptics")

$zip = Join-Path "dist" "RDR_bhaptics.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip
Remove-Item $stage -Recurse -Force
Write-Host "Packaged $zip"
