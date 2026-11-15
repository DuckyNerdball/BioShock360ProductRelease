#Requires -Version 5.1
# Build one highlight reel from a recording and its markers (made by tools\highlights.ps1 or tools\record.ps1).
#   .\tools\reel.ps1                                   # newest recording, every marker
#   .\tools\reel.ps1 -Pick 3,7,15-19,42                # only these markers (numbers from markers.csv / the list printed)
#   .\tools\reel.ps1 -Before 3 -After 8 -Label "Xbox 360 recomp"
#   .\tools\reel.ps1 -Video D:\rec\run1.mp4 -Out D:\rec\reel.mp4
# Markers that sit close together are merged into one shot, so three F10 presses in five seconds give one clip, not
# three copies of the same moment. Every shot gets a short fade in and out, and an optional label in the corner.
# The reel is re-encoded (H.264 + AAC) so the cuts are exact; expect about real time on a mid-range PC.
param(
    [string]$Video, [string[]]$Pick, [double]$Before = 4, [double]$After = 6, [double]$Fade = 0.3,
    [string]$Label, [string[]]$Only, [string]$Out, [int]$Crf = 18, [string]$Font
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
$ffmpeg = Find-Tool "ffmpeg"; $ffprobe = Find-Tool "ffprobe"

if (-not $Video) {
    $dir = Join-Path $env:USERPROFILE "Videos"
    # Newest recording that has markers (a later recording of another game has none and is skipped).
    $v = Get-ChildItem $dir -File -ErrorAction SilentlyContinue | Where-Object { $_.Extension -in ".mkv", ".mp4", ".mov", ".flv" -and $_.BaseName -notmatch '_reel$|^compare' } |
         Where-Object { Test-Path (Join-Path $_.DirectoryName ($_.BaseName + "_highlights\markers.csv")) } |
         Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $v) { throw "No recording with markers found in $dir. Run .\tools\highlights.ps1 first, or pass -Video <path>." }
    $Video = $v.FullName
}
$vf = Get-Item $Video
$hl = Join-Path $vf.DirectoryName ($vf.BaseName + "_highlights")
$csv = Join-Path $hl "markers.csv"
if (-not (Test-Path $csv)) { throw "No markers for this recording ($csv). Run .\tools\highlights.ps1 -Video `"$Video`" first." }
$marks = @(Import-Csv $csv)
for ($i = 0; $i -lt $marks.Count; $i++) { $marks[$i] | Add-Member -NotePropertyName No -NotePropertyValue ($i + 1) -Force }
if ($Only) { $marks = @($marks | Where-Object { $Only -contains $_.Kind }) }
if ($Pick) {
    $want = New-Object System.Collections.Generic.HashSet[int]
    foreach ($p in ($Pick -join "," -split ",")) {
        $p = $p.Trim(); if (-not $p) { continue }
        if ($p -match '^(\d+)-(\d+)$') { foreach ($k in ([int]$Matches[1])..([int]$Matches[2])) { [void]$want.Add($k) } }
        elseif ($p -match '^\d+$') { [void]$want.Add([int]$p) }
        else { throw "-Pick takes marker numbers and ranges, like 3,7,15-19. Got '$p'." }
    }
    $marks = @($marks | Where-Object { $want.Contains([int]$_.No) })
}
if ($marks.Count -eq 0) { throw "No markers left to use." }

$durText = & $ffprobe -v error -show_entries format=duration -of csv=p=0 $vf.FullName
$dur = [double]::Parse(("$durText").Trim(), $inv)

# Merge marker windows that overlap or nearly touch into shots.
$shots = New-Object System.Collections.Generic.List[object]
foreach ($m in ($marks | Sort-Object { [double]::Parse($_.Seconds, $inv) })) {
    $t = [double]::Parse($m.Seconds, $inv)
    $a = [math]::Max(0, $t - $Before); $b = [math]::Min($dur, $t + $After)
    if ($shots.Count -gt 0 -and $a -le ($shots[$shots.Count - 1].To + 1.0)) {
        $shots[$shots.Count - 1].To = [math]::Max($shots[$shots.Count - 1].To, $b)
    } else { $shots.Add([pscustomobject]@{ From = $a; To = $b }) }
}
$total = ($shots | ForEach-Object { $_.To - $_.From } | Measure-Object -Sum).Sum
Write-Host ("{0} markers -> {1} shots, {2:N0} s of reel" -f $marks.Count, $shots.Count, $total)

# Label: drawn with a font file. ffmpeg is run from the font's folder so no drive letter has to be escaped.
$fontDir = $null; $fontFile = $null
if ($Label) {
    $cands = @()
    if ($Font) { $cands += $Font }
    if ($env:WINDIR) { $cands += @("segoeuib.ttf", "arialbd.ttf", "arial.ttf") | ForEach-Object { Join-Path $env:WINDIR "Fonts\$_" } }
    $f = $cands | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
    if ($f) { $fontDir = Split-Path $f; $fontFile = Split-Path $f -Leaf }
    else { Write-Host "No font found for the label; continuing without it (pass -Font <file.ttf>)." -ForegroundColor Yellow; $Label = $null }
}
$safeLabel = if ($Label) { ($Label -replace "[:'\\%,;\[\]=]", "") } else { $null }

if (-not $Out) { $Out = Join-Path $vf.DirectoryName ($vf.BaseName + "_reel.mp4") }
$Out = [IO.Path]::GetFullPath($Out)
$work = Join-Path $hl ("_reel_parts_" + (Get-Date -Format "HHmmss"))
Get-ChildItem $hl -Directory -Filter "_reel_parts*" -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue   # leftovers
New-Item -ItemType Directory $work -Force | Out-Null

$list = New-Object System.Collections.Generic.List[string]
$i = 0
if ($fontDir) { Push-Location $fontDir }
try {
    foreach ($s in $shots) {
        $i++
        $len = $s.To - $s.From
        $fd = [math]::Min($Fade, $len / 4)
        $vfil = "fade=t=in:st=0:d=$(N $fd),fade=t=out:st=$(N ($len - $fd)):d=$(N $fd)"
        if ($safeLabel) { $vfil += ",drawtext=fontfile=${fontFile}:text='${safeLabel}':x=w*0.02:y=h-th-h*0.03:fontsize=h/26:fontcolor=white:box=1:boxcolor=black@0.55:boxborderw=14" }
        $afil = "afade=t=in:st=0:d=$(N $fd),afade=t=out:st=$(N ($len - $fd)):d=$(N $fd)"
        $part = Join-Path $work ("part_{0:000}.mp4" -f $i)
        & $ffmpeg -hide_banner -loglevel error -y -ss (N $s.From) -i $vf.FullName -t (N $len) -vf $vfil -af $afil `
            -c:v libx264 -preset veryfast -crf $Crf -pix_fmt yuv420p -c:a aac -b:a 192k -ar 48000 -ac 2 $part
        if ($LASTEXITCODE -ne 0) { throw "ffmpeg failed on shot $i." }
        $list.Add("file '" + ($part -replace '\\', '/' -replace "'", "'\''") + "'")
        Write-Host ("  shot {0}/{1}  {2:N0} s" -f $i, $shots.Count, $len)
    }
} finally { if ($fontDir) { Pop-Location } }

$listFile = Join-Path $work "list.txt"
[IO.File]::WriteAllLines($listFile, $list, (New-Object Text.UTF8Encoding $false))
& $ffmpeg -hide_banner -loglevel error -y -f concat -safe 0 -i $listFile -c copy -movflags +faststart $Out
if ($LASTEXITCODE -ne 0) { throw "ffmpeg could not join the shots." }
Write-Host "Reel: $Out" -ForegroundColor Green
# The temporary parts are no longer needed. If something (antivirus, a preview) still holds one, leave it for next time.
Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
