#Requires -Version 5.1
# Turn a gameplay recording plus the game's own log into named markers and ready-cut highlight clips.
#   .\tools\highlights.ps1                         # newest recording in your Videos folder, markers only
#   .\tools\highlights.ps1 -Cut                    # also cut a clip around every marker (needs ffmpeg)
#   .\tools\highlights.ps1 -Video D:\rec\run1.mkv -Cut -Before 5 -After 25
#   .\tools\highlights.ps1 -Cut -Only Level,Mark   # only some kinds: Level, Mark, Kill, Death, Story
#   .\tools\highlights.ps1 -Offset 1.5             # markers land late/early? shift them (seconds, + = later)
# How it lines up: the log has a clock time on every line, and the recording's start time is read from its file
# name (OBS default "2026-11-14 19-30-05.mkv") or, failing that, from when the file was created.
# Markers come from the log:
#   Level  a map was loaded (Lighthouse, Welcome to Rapture, ...)
#   Mark   you pressed F10 in game
#   Kill / Death / Story   lines the game writes as "MARK kill|death|story" (build 2026-11-14 or newer)
# Output goes next to the recording, in <name>_highlights\: markers.csv, chapters.txt and (with -Cut) the clips.
# Nothing here touches game files; the clips are your own recording.
param(
    [string]$Video, [string[]]$Log, [double]$Before = 10, [double]$After = 20, [double]$Offset = 0,
    [switch]$Cut, [string]$Out, [string[]]$Only,
    [datetime]$StartedAt   # exact time the recording started (record.ps1 passes it); default: from the file name
)
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$inv = [Globalization.CultureInfo]::InvariantCulture

if (-not $Video) {
    $dir = Join-Path $env:USERPROFILE "Videos"
    $v = Get-ChildItem $dir -File -ErrorAction SilentlyContinue | Where-Object { $_.Extension -in ".mkv", ".mp4", ".mov", ".flv" } |
         Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $v) { throw "No recording found in $dir. Pass one: .\tools\highlights.ps1 -Video <path>" }
    $Video = $v.FullName
}
if (-not (Test-Path $Video)) { throw "Not found: $Video" }
$vf = Get-Item $Video

# When the recording started.
$start = $vf.CreationTime
if ($PSBoundParameters.ContainsKey('StartedAt')) { $start = $StartedAt }
elseif ($vf.BaseName -match '(\d{4})-(\d{2})-(\d{2})[ _T](\d{2})-(\d{2})-(\d{2})') {
    $start = Get-Date -Year $Matches[1] -Month $Matches[2] -Day $Matches[3] -Hour $Matches[4] -Minute $Matches[5] -Second $Matches[6] -Millisecond 0
}
# How long it is.
$ffprobe = Get-Command ffprobe -ErrorAction SilentlyContinue
$dur = ($vf.LastWriteTime - $start).TotalSeconds
if ($ffprobe) {
    $d = & $ffprobe.Source -v error -show_entries format=duration -of csv=p=0 $vf.FullName 2>$null
    $p = 0.0
    if ($d -and [double]::TryParse(("$d").Trim(), [Globalization.NumberStyles]::Float, $inv, [ref]$p) -and $p -gt 0) { $dur = $p }
}
if ($dur -le 0) { throw "Could not work out the recording's length." }
$end = $start.AddSeconds($dur)
Write-Host ("Recording: {0}`n  started {1:yyyy-MM-dd HH:mm:ss}, {2:N0} s long" -f $vf.Name, $start, $dur)

# Which logs cover it.
if (-not $Log) {
    $cands = @(Get-ChildItem (Join-Path $Root "logs") -Filter "play_*.log" -ErrorAction SilentlyContinue)
    foreach ($extra in "out\recomplauncher\bioshock.log", "out\recomplauncher\bioshock.log.old") {
        $e = Join-Path $Root $extra; if (Test-Path $e) { $cands += Get-Item $e }
    }
    $Log = @($cands | Where-Object { $_.LastWriteTime -ge $start.AddSeconds(-5) -and $_.CreationTime -le $end.AddMinutes(1) } | ForEach-Object FullName)
    if (-not $Log) { throw "No game log overlaps this recording. Was the game started from this folder while recording? Pass -Log <file> to force one." }
}

function Get-LevelName([string]$map) {
    $m = $map.ToLower()
    if ($m -match 'lighthouse') { return "Lighthouse" }
    if ($m -match 'welcome')    { return "Welcome to Rapture" }
    if ($m -match 'medical')    { return "Medical Pavilion" }
    return $map
}

$rxTime = [regex]'^\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3})\]'
$rxMap  = [regex]"file 'maps\\([^']+?)\.bsm'"
$raw = New-Object System.Collections.Generic.List[object]
foreach ($lf in $Log) {
    Write-Host "  log: $lf"
    foreach ($line in [IO.File]::ReadLines($lf)) {
        $kind = $null; $label = $null
        if     ($line.Contains("USER MARK"))  { $kind = "Mark";  $label = "Mark (F10)" }
        elseif ($line.Contains("MARK kill"))  { $kind = "Kill";  $label = "Kill" }
        elseif ($line.Contains("MARK death")) { $kind = "Death"; $label = "Death" }
        elseif ($line.Contains("MARK story")) { $kind = "Story"; $label = "Story beat" }
        elseif ($line.Contains("Jack died") -or $line.Contains("event: Jack.OnKilled")) { $kind = "Death"; $label = "Death" }
        elseif ($line.Contains("file 'maps\")) {
            $mm = $rxMap.Match($line)
            if ($mm.Success) {
                $map = $mm.Groups[1].Value
                if ($map -ne "Entry" -and $map -notmatch '_int$') { $kind = "Level"; $label = "Level: " + (Get-LevelName $map) }
            }
        }
        if (-not $kind) { continue }
        $tm = $rxTime.Match($line); if (-not $tm.Success) { continue }
        $t = [datetime]::ParseExact($tm.Groups[1].Value, "yyyy-MM-dd HH:mm:ss.fff", $inv)
        $raw.Add([pscustomobject]@{ Time = $t; Kind = $kind; Label = $label })
    }
}

# Tidy: in order, inside the recording, repeats of the same thing collapsed.
$gap = @{ Level = 60; Death = 15; Kill = 2; Story = 5; Mark = 1 }
$last = @{}; $count = @{}
$marks = New-Object System.Collections.Generic.List[object]
foreach ($r in ($raw | Sort-Object Time)) {
    if ($Only -and ($Only -notcontains $r.Kind)) { continue }
    $sec = ($r.Time - $start).TotalSeconds + $Offset
    if ($sec -lt 0 -or $sec -gt $dur) { continue }
    $key = if ($r.Kind -eq "Level") { $r.Label } else { $r.Kind }
    if ($last.ContainsKey($key) -and ($sec - $last[$key]) -lt $gap[$r.Kind]) { $last[$key] = $sec; continue }
    $last[$key] = $sec
    $count[$r.Kind] = 1 + [int]$count[$r.Kind]
    $label = if ($r.Kind -in "Kill", "Death", "Mark", "Story") { "{0} {1}" -f $r.Label, $count[$r.Kind] } else { $r.Label }
    $ts = [timespan]::FromSeconds([math]::Floor($sec))
    $stamp = if ($ts.TotalHours -ge 1) { "{0}:{1:00}:{2:00}" -f [int][math]::Floor($ts.TotalHours), $ts.Minutes, $ts.Seconds } else { "{0}:{1:00}" -f $ts.Minutes, $ts.Seconds }
    $marks.Add([pscustomobject]@{ At = $stamp; Seconds = [math]::Round($sec, 2); Kind = $r.Kind; Label = $label; Clock = $r.Time.ToString("HH:mm:ss") })
}
if ($marks.Count -eq 0) {
    Write-Host "No markers fell inside this recording. Press F10 in game to add your own, or check -Offset." -ForegroundColor Yellow
    return
}

if (-not $Out) { $Out = Join-Path $vf.DirectoryName ($vf.BaseName + "_highlights") }
New-Item -ItemType Directory $Out -Force | Out-Null
$marks | Export-Csv (Join-Path $Out "markers.csv") -NoTypeInformation -Encoding UTF8
@("0:00 Start") + ($marks | ForEach-Object { "{0} {1}" -f $_.At, $_.Label }) | Set-Content (Join-Path $Out "chapters.txt") -Encoding UTF8
foreach ($m in $marks) { Write-Host ("  {0,8}  {1}" -f $m.At, $m.Label) }
Write-Host ("{0} markers -> {1}" -f $marks.Count, $Out) -ForegroundColor Green

if ($Cut) {
    $ffmpeg = Get-Command ffmpeg -ErrorAction SilentlyContinue
    if (-not $ffmpeg) { throw "ffmpeg is not installed. Install it, open a new PowerShell window and run this again:  winget install Gyan.FFmpeg" }
    $i = 0; $busyUntil = -1.0
    foreach ($m in $marks) {
        $from = [math]::Max(0, $m.Seconds - $Before)
        if ($m.Kind -eq "Kill" -and $m.Seconds -lt $busyUntil) { continue }   # already inside the previous clip
        $len = [math]::Min($dur - $from, $Before + $After)
        $busyUntil = $from + $len
        $i++
        $safe = ($m.Label -replace '[^A-Za-z0-9 ]', '' -replace ' +', '_')
        $name = "{0:00}_{1}_{2}{3}" -f $i, $safe, ($m.At -replace ':', '-'), $vf.Extension
        $dst = Join-Path $Out $name
        & $ffmpeg.Source -hide_banner -loglevel error -y -ss $from.ToString("0.###", $inv) -i $vf.FullName -t $len.ToString("0.###", $inv) -c copy -avoid_negative_ts make_zero $dst
        if ($LASTEXITCODE -ne 0) { Write-Host "  ffmpeg failed on $name" -ForegroundColor Red } else { Write-Host "  $name" }
    }
    Write-Host ("{0} clips cut ({1} s before, {2} s after each marker)." -f $i, $Before, $After) -ForegroundColor Green
}
