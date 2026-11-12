#Requires -Version 5.1
# Extract YOUR BioShock (Xbox 360) disc 1 image into a folder and check it is the supported build.
#   .\tools\extract_disc.ps1                       # asks for the .iso, extracts to .\game_base1
#   .\tools\extract_disc.ps1 -Iso D:\BioShock.iso -Out D:\Games\BioShock360
# The folder it makes (the one with default.xex) is what RecompLauncher asks for as "Game folder", and what
# Install.ps1 builds from. Uses extract-xiso (XboxDev, open source), downloaded to tools\bin on first use.
# Nothing from the game is downloaded: you supply your own legally dumped disc image.
param([string]$Iso, [string]$Out)
$ErrorActionPreference = "Stop"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$ProgressPreference = "SilentlyContinue"
$Root = Split-Path -Parent $PSScriptRoot
$ExpectedXexSha1 = "1be6a0fadea96b3825e6213a4a4ae1a0e4c96c91"   # BioShock (360) disc 1 default.xex, title ID 545407D8
$BinDir = Join-Path $Root "tools\bin"
if (-not $Out) { $Out = Join-Path $Root "game_base1" }

if (-not $Iso) {
    Add-Type -AssemblyName System.Windows.Forms
    $dlg = New-Object System.Windows.Forms.OpenFileDialog
    $dlg.Title = "Select YOUR BioShock (Xbox 360) disc 1 image"
    $dlg.Filter = "Xbox 360 disc image (*.iso)|*.iso"
    if ($dlg.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { throw "No disc image selected." }
    $Iso = $dlg.FileName
}
if (-not (Test-Path $Iso)) { throw "Not found: $Iso" }

$xex = Join-Path $Out "default.xex"
if (Test-Path $xex) { Write-Host "Already extracted: $Out" -ForegroundColor Green }
else {
    $xiso = Get-ChildItem $BinDir -Recurse -Filter "extract-xiso.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $xiso) {
        Write-Host "Downloading extract-xiso (XboxDev/extract-xiso, open source)..."
        $rel = Invoke-RestMethod -Headers @{ "User-Agent" = "bioshock-recomp-install" } "https://api.github.com/repos/XboxDev/extract-xiso/releases/latest"
        $asset = $rel.assets | Where-Object { $_.name -match "win" -and $_.name -match "\.zip$" } | Select-Object -First 1
        if (-not $asset) { throw "Couldn't find a Windows extract-xiso download. Get it from https://github.com/XboxDev/extract-xiso/releases and put extract-xiso.exe in tools\bin\." }
        $zip = Join-Path $env:TEMP $asset.name
        Invoke-WebRequest $asset.browser_download_url -OutFile $zip -UseBasicParsing
        New-Item -ItemType Directory $BinDir -Force | Out-Null
        Expand-Archive $zip -DestinationPath $BinDir -Force; Remove-Item $zip
        $xiso = Get-ChildItem $BinDir -Recurse -Filter "extract-xiso.exe" | Select-Object -First 1
        if (-not $xiso) { throw "extract-xiso.exe not found after download." }
    }
    Write-Host "Extracting $Iso -> $Out (about 2 GB, a few minutes)..."
    $old = $ErrorActionPreference; $ErrorActionPreference = "Continue"
    try { & $xiso.FullName -d $Out $Iso 2>&1 | Out-Null; $code = $LASTEXITCODE } finally { $ErrorActionPreference = $old }
    if (-not (Test-Path $xex)) { throw "Extraction finished (exit $code) but default.xex is missing in $Out. Is this BioShock disc 1 (not the bonus disc)?" }
}

$h = (Get-FileHash $xex -Algorithm SHA1).Hash.ToLower()
if ($h -ne $ExpectedXexSha1) {
    Write-Host "default.xex SHA-1 is $h" -ForegroundColor Red
    Write-Host "Supported: $ExpectedXexSha1 (title ID 545407D8, 2007-06-22 build). Other regions/versions are not supported yet." -ForegroundColor Red
    exit 1
}
Write-Host "Supported build confirmed. Your game folder is:" -ForegroundColor Green
Write-Host "  $Out"
Write-Host "In RecompLauncher, choose that folder when it asks for your copy (Game folder)."
