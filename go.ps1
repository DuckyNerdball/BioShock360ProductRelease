# Build the app (VS dev shell) and launch the game. Usage from repo root:  .\go
param([switch]$NoBuild)
$Root = $PSScriptRoot
if (-not $NoBuild) {
    Push-Location (Join-Path $Root "app")
    try { cmake --build --preset win-amd64-relwithdebinfo; if ($LASTEXITCODE -ne 0) { throw "Build failed" } } finally { Pop-Location }
}
& (Join-Path $Root "tools\play.ps1") @args
