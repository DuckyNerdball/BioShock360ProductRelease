#Requires -Version 5.1
# Put two or three videos side by side in one file, each with a label, for a like-for-like comparison.
#   .\tools\compare.ps1 -Videos recomp.mp4,pc.mp4 -Labels "Xbox 360 recomp","PC (2007)"
#   .\tools\compare.ps1 -Videos recomp.mp4,pc.mp4,remaster.mp4 -Labels "Xbox 360 recomp","PC (2007)","Remastered (2016)"
#   .\tools\compare.ps1 -Videos a.mp4,b.mp4 -Starts 12.5,40 -Length 30      # line the same moment up: where each one starts
#   .\tools\compare.ps1 -Videos a.mp4,b.mp4 -Speed 0.25                     # slow motion (shows 30 vs 60 fps physics)
# Sound comes from the first video unless you pass -AudioFrom 2 (or 3). With -Speed the result has no sound.
param(
    [Parameter(Mandatory = $true)][string[]]$Videos, [string[]]$Labels, [string[]]$Starts, [double]$Length = 0,
    [int]$Height = 0, [int]$AudioFrom = 1, [double]$Speed = 1, [string]$Out, [int]$Crf = 18, [string]$Font
)
$ErrorActionPreference = "Stop"
$inv = [Globalization.CultureInfo]::InvariantCulture
function N([double]$x) { return $x.ToString("0.###", $inv) }
function Find-Tool([string]$name) {
    $c = Get-Command $name -ErrorAction SilentlyContinue
    if ($c) { return $c.Source }
    if ($env:LOCALAPPDATA) {
        $l = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Links\$name.exe"
        if (Test-Path $l) { return $l }
    }
    throw "$name is not installed. Install it and open a new PowerShell window:  winget install Gyan.FFmpeg"
}
$ffmpeg = Find-Tool "ffmpeg"

$Videos = @($Videos -join "," -split "," | ForEach-Object { $_.Trim() } | Where-Object { $_ })
if ($Videos.Count -lt 2 -or $Videos.Count -gt 3) { throw "Give two or three videos." }
$files = @($Videos | ForEach-Object { if (-not (Test-Path $_)) { throw "Not found: $_" }; (Get-Item $_).FullName })
if ($Labels) { $Labels = @($Labels -join "," -split "," | ForEach-Object { $_.Trim() } | Where-Object { $_ }) }
$startSec = @()
if ($Starts) { $startSec = @($Starts -join "," -split "," | Where-Object { $_.Trim() } | ForEach-Object { [double]::Parse($_.Trim(), $inv) }) }
if ($Labels -and $Labels.Count -ne $files.Count) { throw "Give one label per video ($($files.Count))." }
if ($Starts -and $startSec.Count -ne $files.Count) { throw "Give one start time per video ($($files.Count))." }
if ($AudioFrom -lt 1 -or $AudioFrom -gt $files.Count) { throw "-AudioFrom must be 1..$($files.Count)." }
if ($Speed -le 0) { throw "-Speed must be above 0 (0.25 = quarter speed)." }
if ($Height -le 0) { $Height = if ($files.Count -eq 3) { 720 } else { 1080 } }

$fontDir = $null; $fontFile = $null
if ($Labels) {
    $cands = @()
    if ($Font) { $cands += $Font }
    if ($env:WINDIR) { $cands += @("segoeuib.ttf", "arialbd.ttf", "arial.ttf") | ForEach-Object { Join-Path $env:WINDIR "Fonts\$_" } }
    $f = $cands | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
    if ($f) { $fontDir = Split-Path $f; $fontFile = Split-Path $f -Leaf }
    else { Write-Host "No font found for the labels; continuing without them (pass -Font <file.ttf>)." -ForegroundColor Yellow; $Labels = $null }
}

$inArgs = @(); $chain = @(); $tags = ""
for ($i = 0; $i -lt $files.Count; $i++) {
    if ($Starts) { $inArgs += @("-ss", (N $startSec[$i])) }
    if ($Length -gt 0) { $inArgs += @("-t", (N $Length)) }
    $inArgs += @("-i", $files[$i])
    $f = "[${i}:v]scale=-2:${Height},setsar=1"
    if ($Speed -ne 1) { $f += ",setpts=PTS/$(N $Speed)" }
    if ($Labels) {
        $t = ($Labels[$i] -replace "[:'\\%,;\[\]=]", "")
        $f += ",drawtext=fontfile=${fontFile}:text='${t}':x=w*0.03:y=h-th-h*0.04:fontsize=h/22:fontcolor=white:box=1:boxcolor=black@0.55:boxborderw=14"
    }
    $chain += "$f[v$i]"; $tags += "[v$i]"
}
$chain += "${tags}hstack=inputs=$($files.Count):shortest=1[v]"
if (-not $Out) { $Out = Join-Path (Split-Path $files[0]) ("compare_" + [IO.Path]::GetFileNameWithoutExtension($files[0]) + ".mp4") }
$Out = [IO.Path]::GetFullPath($Out)

$args2 = @("-hide_banner", "-loglevel", "error", "-y") + $inArgs + @("-filter_complex", ($chain -join ";"), "-map", "[v]")
if ($Speed -eq 1) { $args2 += @("-map", "$($AudioFrom - 1):a?", "-c:a", "aac", "-b:a", "192k") } else { $args2 += "-an" }
$args2 += @("-c:v", "libx264", "-preset", "veryfast", "-crf", "$Crf", "-pix_fmt", "yuv420p", "-shortest", "-movflags", "+faststart", $Out)
if ($fontDir) { Push-Location $fontDir }
try { & $ffmpeg @args2 } finally { if ($fontDir) { Pop-Location } }
if ($LASTEXITCODE -ne 0) { throw "ffmpeg failed." }
Write-Host "Comparison: $Out" -ForegroundColor Green
