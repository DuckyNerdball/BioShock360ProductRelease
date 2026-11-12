# Assemble a LOCAL folder that RecompLauncher can add as a build (Settings > Developers > Developer tools > "Add a build").
#   .\tools\package_local.ps1                 # -> out\recomplauncher\  (bioshock.exe, its DLLs, recomplauncher.json)
# NEVER upload this folder or attach it to a release: bioshock.exe is code generated from YOUR disc. This project
# distributes scripts and source only; every player builds their own copy. out\ is gitignored for that reason.
param([string]$Out, [string]$Preset = "win-amd64-relwithdebinfo")
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
if (-not $Out) { $Out = Join-Path $Root "out\recomplauncher" }
$Bin = Join-Path $Root "app\out\build\$Preset"
if (-not (Test-Path (Join-Path $Bin "bioshock.exe"))) { throw "No built game at $Bin. Build it first: .\go.ps1" }
New-Item -ItemType Directory $Out, (Join-Path $Out "run"), (Join-Path $Out "userdata") -Force | Out-Null
Copy-Item (Join-Path $Bin "bioshock.exe") $Out -Force
Copy-Item (Join-Path $Bin "*.dll") $Out -Force
Copy-Item (Join-Path $Root "recomplauncher.json") $Out -Force
Write-Host "Local RecompLauncher build: $Out" -ForegroundColor Green
Write-Host "In RecompLauncher: Settings > Developers > Developer tools > Add a build > pick that folder."
Write-Host "When it asks for the game, pick your extracted disc folder: $(Join-Path $Root 'game_base1')"
Write-Host "Do not upload this folder: it contains code generated from your disc." -ForegroundColor Yellow
