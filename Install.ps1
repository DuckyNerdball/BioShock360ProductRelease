#Requires -Version 5.1
<#
BioShock 360 Recomp - one-shot installer for a fresh Windows PC.

  Right-click > "Run with PowerShell", or:
    powershell -ExecutionPolicy Bypass -File .\Install.ps1 [-Iso <path to your BioShock disc 1 .iso>]

What it does (safe to re-run; every step skips what is already done):
  1. Installs build tools: Git, CMake, Ninja, Python, Visual Studio 2022 C++ + Clang, LLVM 20+ (winget)
  2. Gets extract-xiso and extracts YOUR disc image into game_base1\
  3. Checks default.xex is the exact build this project targets (SHA-1)
  4. Builds the patched ReXGlue SDK, generates the C++ from your XEX, compiles bioshock.exe
  5. Puts a "BioShock Recomp" launcher shortcut on your desktop

Needs: Windows 10/11 x64, ~60 GB free, internet, admin rights (Visual Studio), 1-2 hours the first time.
Nothing from the game is downloaded or shipped: you supply your own legally dumped disc image.
#>
param(
    [string]$Iso,
    [switch]$SkipPrereqs,
    [switch]$NoShortcut
)
$ErrorActionPreference = "Stop"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$ProgressPreference = "SilentlyContinue"
$Root = $PSScriptRoot
$ExpectedXexSha1 = "1be6a0fadea96b3825e6213a4a4ae1a0e4c96c91"   # BioShock (360) base disc default.xex, TitleID 545407D8
$BinDir = Join-Path $Root "tools\bin"

function Step($m) { Write-Host "`n=== $m" -ForegroundColor Cyan }
function Ok($m)   { Write-Host "  [ok] $m" -ForegroundColor Green }
function Die($m)  { Write-Host "`n  [STOP] $m`n" -ForegroundColor Red; Read-Host "Press Enter to close"; exit 1 }

# --- elevate (Visual Studio install and SDK symlinks need it)
$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) {
    Write-Host "Restarting as Administrator..."
    $a = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$PSCommandPath`"")
    if ($Iso) { $a += @("-Iso", "`"$Iso`"") }
    if ($SkipPrereqs) { $a += "-SkipPrereqs" }; if ($NoShortcut) { $a += "-NoShortcut" }
    Start-Process powershell -Verb RunAs -ArgumentList $a
    exit
}
Set-Location $Root
$t0 = Get-Date

Step "1. Build tools"
if ($SkipPrereqs) { Ok "skipped (-SkipPrereqs)" }
else {
    & (Join-Path $Root "tools\bootstrap.ps1") -SkipGhidra -SkipXenia -SkipDiscs -SkipSetup -MinFreeGB 60
    $env:Path = [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [Environment]::GetEnvironmentVariable("Path", "User")
}

Step "2. Your game files"
$xex = Join-Path $Root "game_base1\default.xex"
if (Test-Path $xex) { Ok "game_base1\ already extracted" }
else {
    if (-not $Iso) {
        Add-Type -AssemblyName System.Windows.Forms
        $dlg = New-Object System.Windows.Forms.OpenFileDialog
        $dlg.Title = "Select YOUR BioShock (Xbox 360) disc 1 image"
        $dlg.Filter = "Xbox 360 disc image (*.iso)|*.iso"
        if ($dlg.ShowDialog() -ne "OK") { Die "No disc image selected." }
        $Iso = $dlg.FileName
    }
    if (-not (Test-Path $Iso)) { Die "Not found: $Iso" }
    $xiso = Get-ChildItem $BinDir -Recurse -Filter "extract-xiso.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $xiso) {
        Write-Host "  Downloading extract-xiso (XboxDev/extract-xiso, open source)..."
        $rel = Invoke-RestMethod -Headers @{ "User-Agent" = "bioshock-recomp-install" } "https://api.github.com/repos/XboxDev/extract-xiso/releases/latest"
        $asset = $rel.assets | Where-Object { $_.name -match "win" -and $_.name -match "\.zip$" } | Select-Object -First 1
        if (-not $asset) { Die "Couldn't find a Windows extract-xiso download. Get it from https://github.com/XboxDev/extract-xiso/releases, put extract-xiso.exe in tools\bin\ and re-run." }
        $zip = Join-Path $env:TEMP $asset.name
        Invoke-WebRequest $asset.browser_download_url -OutFile $zip -UseBasicParsing
        New-Item -ItemType Directory $BinDir -Force | Out-Null
        Expand-Archive $zip -DestinationPath $BinDir -Force; Remove-Item $zip
        $xiso = Get-ChildItem $BinDir -Recurse -Filter "extract-xiso.exe" | Select-Object -First 1
        if (-not $xiso) { Die "extract-xiso.exe not found after download." }
    }
    Ok "extract-xiso: $($xiso.FullName)"
    & (Join-Path $Root "tools\inspect_disc.ps1") -Iso $Iso -Name base1 -ExtractXiso $xiso.FullName
    if (-not (Test-Path $xex)) { Die "Extraction finished but game_base1\default.xex is missing. Is this BioShock disc 1 (not the bonus/add-on disc)?" }
}

Step "3. Check it's the supported build"
$h = (Get-FileHash $xex -Algorithm SHA1).Hash.ToLower()
if ($h -ne $ExpectedXexSha1) {
    Die "default.xex SHA-1 is $h`n  Supported: $ExpectedXexSha1 (BioShock 360, TitleID 545407D8, 2007-06-22 build).`n  Every address in this project is tied to that exact file. Other regions/versions are not supported yet."
}
Ok "default.xex matches ($h)"

Step "4. Build (SDK + code generation + compile; 20-60 min the first time)"
. (Join-Path $Root "tools\vsenv.ps1")
& (Join-Path $Root "tools\setup.ps1")
Push-Location (Join-Path $Root "app")
try {
    function Run([string[]]$a) { & cmake @a; if ($LASTEXITCODE -ne 0) { throw "cmake $($a -join ' ') failed" } }
    Run @("--preset", "win-amd64-relwithdebinfo")
    Run @("--build", "--preset", "win-amd64-relwithdebinfo", "--target", "bioshock_codegen")
    Run @("--preset", "win-amd64-relwithdebinfo")   # re-configure: generated sources list only exists after codegen
    Run @("--build", "--preset", "win-amd64-relwithdebinfo")
} catch { Pop-Location; Die "Build failed: $($_.Exception.Message)" }
Pop-Location
$exe = Join-Path $Root "app\out\build\win-amd64-relwithdebinfo\bioshock.exe"
if (-not (Test-Path $exe)) { Die "bioshock.exe was not produced." }
Ok "bioshock.exe built"

Step "5. Launcher shortcut"
if ($NoShortcut) { Ok "skipped" }
else {
    $lnk = Join-Path ([Environment]::GetFolderPath("Desktop")) "BioShock Recomp.lnk"
    $ws = New-Object -ComObject WScript.Shell
    $s = $ws.CreateShortcut($lnk)
    $s.TargetPath = Join-Path $Root "BioShock.cmd"
    $s.WorkingDirectory = $Root
    $s.Description = "BioShock 360 Recomp launcher"
    $s.Save()
    Ok "Desktop shortcut: $lnk"
}

Write-Host ("`nDone in {0:N0} min. Start the game with the 'BioShock Recomp' desktop shortcut (or BioShock.cmd)." -f ((Get-Date) - $t0).TotalMinutes) -ForegroundColor Green
Read-Host "Press Enter to close"
