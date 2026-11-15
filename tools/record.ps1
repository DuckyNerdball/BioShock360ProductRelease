#Requires -Version 5.1
# One command for a recorded session: starts an OBS recording, starts the game, and when you quit the game it
# stops the recording and cuts the highlight clips (tools\highlights.ps1).
#   .\tools\record.ps1                 # record + play + clips
#   .\tools\record.ps1 -Build          # build the game first
#   .\tools\record.ps1 -Before 5 -After 30 -Only Level,Mark
# The game starts with the options you last used with .\go.ps1 (it remembers them).
# In game, press F10 at every moment you want a clip of.
# One-time OBS setup (the script tells you if it is missing):
#   OBS > Tools > WebSocket Server Settings > tick "Enable WebSocket server", port 4455.
#   Either untick "Enable Authentication", or keep it and save the password in run\obs_password.txt.
#   Add a source that captures the game (Game Capture or Display Capture) so the recording is not black.
param(
    [switch]$Build, [double]$Before = 10, [double]$After = 20, [string[]]$Only, [switch]$NoCut,
    [int]$ObsPort = 4455, [string]$ObsPassword
)
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot

# --- ffmpeg (for cutting clips): install once, and find it even in this same window --------------------------------
function Find-Ffmpeg {
    $c = Get-Command ffmpeg -ErrorAction SilentlyContinue
    if ($c) { return $true }
    $links = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Links"
    if (Test-Path (Join-Path $links "ffmpeg.exe")) { $env:PATH = "$links;$env:PATH"; return $true }
    return $false
}
if (-not $NoCut -and -not (Find-Ffmpeg)) {
    Write-Host "Installing ffmpeg (one time)..." -ForegroundColor Cyan
    winget install --id Gyan.FFmpeg -e --accept-source-agreements --accept-package-agreements | Out-Host
    if (-not (Find-Ffmpeg)) { Write-Host "ffmpeg is installed but not visible yet. Clips will be skipped this run; open a new PowerShell window next time." -ForegroundColor Yellow; $NoCut = $true }
}

# --- OBS over its WebSocket server (obs-websocket 5, built into OBS 28 and newer) ---------------------------------
if (-not $ObsPassword) {
    $pf = Join-Path $Root "run\obs_password.txt"
    if (Test-Path $pf) { $ObsPassword = (Get-Content $pf -Raw).Trim() }
}
function Send-Ws($ws, [string]$text) {
    $bytes = [Text.Encoding]::UTF8.GetBytes($text)
    $seg = New-Object System.ArraySegment[byte] -ArgumentList @(, $bytes)
    $ws.SendAsync($seg, [System.Net.WebSockets.WebSocketMessageType]::Text, $true, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
}
function Receive-Ws($ws) {
    $buf = New-Object byte[] 65536
    $ms = New-Object IO.MemoryStream
    do {
        $seg = New-Object System.ArraySegment[byte] -ArgumentList @(, $buf)
        $cts = New-Object Threading.CancellationTokenSource
        $cts.CancelAfter(10000)
        $r = $ws.ReceiveAsync($seg, $cts.Token).GetAwaiter().GetResult()
        if ($r.MessageType -eq [System.Net.WebSockets.WebSocketMessageType]::Close) { throw "OBS closed the connection." }
        $ms.Write($buf, 0, $r.Count)
    } while (-not $r.EndOfMessage)
    return ([Text.Encoding]::UTF8.GetString($ms.ToArray()) | ConvertFrom-Json)
}
function Get-Sha256B64([string]$s) {
    $sha = [Security.Cryptography.SHA256]::Create()
    return [Convert]::ToBase64String($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($s)))
}
# Sends one request to OBS and returns its response data. Opens and closes its own connection.
function Invoke-Obs([string]$Request) {
    $ws = New-Object System.Net.WebSockets.ClientWebSocket
    try {
        $cts = New-Object Threading.CancellationTokenSource
        $cts.CancelAfter(5000)
        $ws.ConnectAsync([Uri]"ws://127.0.0.1:$ObsPort", $cts.Token).GetAwaiter().GetResult()
        $hello = Receive-Ws $ws
        $ident = @{ op = 1; d = @{ rpcVersion = 1; eventSubscriptions = 0 } }
        if ($hello.d.authentication) {
            if (-not $ObsPassword) { throw "OBS wants a password. Save it in run\obs_password.txt (OBS > Tools > WebSocket Server Settings > Show Connect Info), or untick Enable Authentication there." }
            $secret = Get-Sha256B64 ($ObsPassword + $hello.d.authentication.salt)
            $ident.d.authentication = Get-Sha256B64 ($secret + $hello.d.authentication.challenge)
        }
        Send-Ws $ws ($ident | ConvertTo-Json -Depth 5 -Compress)
        $idd = Receive-Ws $ws
        if ($idd.op -ne 2) { throw "OBS did not accept the connection (wrong password?)." }
        $id = [guid]::NewGuid().ToString()
        Send-Ws $ws (@{ op = 6; d = @{ requestType = $Request; requestId = $id } } | ConvertTo-Json -Depth 5 -Compress)
        for ($i = 0; $i -lt 50; $i++) {
            $m = Receive-Ws $ws
            if ($m.op -eq 7 -and $m.d.requestId -eq $id) {
                if (-not $m.d.requestStatus.result) { throw ("OBS refused {0}: code {1} {2}" -f $Request, $m.d.requestStatus.code, $m.d.requestStatus.comment) }
                return $m.d.responseData
            }
        }
        throw "OBS did not answer $Request."
    } finally { $ws.Dispose() }
}
function Test-ObsUp { try { [void](Invoke-Obs "GetVersion"); return $true } catch { $script:ObsErr = $_.Exception.Message; return $false } }

if (-not (Test-ObsUp)) {
    if (-not (Get-Process obs64 -ErrorAction SilentlyContinue)) {
        $obs = @("$env:ProgramFiles\obs-studio\bin\64bit\obs64.exe", "${env:ProgramFiles(x86)}\obs-studio\bin\64bit\obs64.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
        if (-not $obs) { throw "OBS Studio is not installed. Install it:  winget install OBSProject.OBSStudio   then do the one-time setup at the top of this script." }
        Write-Host "Starting OBS..." -ForegroundColor Cyan
        Start-Process $obs -WorkingDirectory (Split-Path $obs) -ArgumentList "--minimize-to-tray"
        for ($i = 0; $i -lt 30 -and -not (Test-ObsUp); $i++) { Start-Sleep -Seconds 1 }
    }
    if (-not (Test-ObsUp)) {
        Write-Host "Can't reach OBS: $script:ObsErr" -ForegroundColor Red
        Write-Host "One-time setup: OBS > Tools > WebSocket Server Settings > tick 'Enable WebSocket server' (port $ObsPort)."
        Write-Host "Then untick 'Enable Authentication', or save the password in run\obs_password.txt. Run this again after."
        exit 1
    }
}

# --- record, play, stop, cut --------------------------------------------------------------------------------------
$status = Invoke-Obs "GetRecordStatus"
if ($status.outputActive) { Write-Host "OBS is already recording - stopping that recording first." -ForegroundColor Yellow; [void](Invoke-Obs "StopRecord"); Start-Sleep -Seconds 2 }
[void](Invoke-Obs "StartRecord")
$t0 = Get-Date
Write-Host ("Recording started {0:HH:mm:ss}. Press F10 in game at every moment you want a clip of." -f $t0) -ForegroundColor Green

$video = $null
try {
    $go = Join-Path $Root "go.ps1"
    if ($Build) { & $go } else { & $go -NoBuild }
} finally {
    try { $video = (Invoke-Obs "StopRecord").outputPath }
    catch {
        # Code 501 = OBS was no longer recording (stopped by hand, or it stopped itself). Use the newest file it saved.
        try {
            $recDir = (Invoke-Obs "GetRecordDirectory").recordDirectory
            $newest = Get-ChildItem $recDir -File | Where-Object { $_.Extension -in ".mkv", ".mp4", ".mov", ".flv" -and $_.LastWriteTime -ge $t0 } |
                      Sort-Object LastWriteTime -Descending | Select-Object -First 1
            if ($newest) { $video = $newest.FullName; Write-Host "OBS had already stopped recording; using $video" -ForegroundColor Yellow }
        } catch { }
        if (-not $video) { Write-Host "Could not stop the OBS recording: $($_.Exception.Message). Stop it in OBS, then run .\tools\highlights.ps1 -Cut" -ForegroundColor Red }
    }
}
if (-not $video) { exit 1 }
Write-Host "Recording saved: $video" -ForegroundColor Green
for ($i = 0; $i -lt 20; $i++) {   # wait until OBS has finished writing the file
    try { $fs = [IO.File]::Open($video, 'Open', 'Read', 'None'); $fs.Close(); break } catch { Start-Sleep -Milliseconds 500 }
}

$hl = @{ Video = $video; StartedAt = $t0; Before = $Before; After = $After }
if (-not $NoCut) { $hl.Cut = $true }
if ($Only) { $hl.Only = $Only }
& (Join-Path $PSScriptRoot "highlights.ps1") @hl
$outDir = Join-Path (Split-Path $video) ([IO.Path]::GetFileNameWithoutExtension($video) + "_highlights")
if (Test-Path $outDir) { Start-Process explorer.exe $outDir }
