# Build the app (VS dev shell) and launch the game. Usage from repo root:  .\go
param([switch]$NoBuild)
$Root = $PSScriptRoot
if (-not $NoBuild) {
    Push-Location (Join-Path $Root "app")
    try { cmake --build --preset win-amd64-relwithdebinfo 2>&1 | Tee-Object -FilePath (Join-Path $Root "logs\build_last.log"); if ($LASTEXITCODE -ne 0) { throw "Build failed (see logs\build_last.log)" } } finally { Pop-Location }
}
# -Remember: play.ps1 saves the options given here and reuses them when you run plain .\go.ps1 (.\go.ps1 -Default clears).
& (Join-Path $Root "tools\play.ps1") -Remember @args
