#Requires -Version 5.1
# BioShock 360 Recomp launcher (Windows Forms; no extra installs). Open with BioShock.cmd or the desktop shortcut.
# Settings are remembered in run\launcher.json. Every button just calls the scripts in tools\.
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()
$Root = $PSScriptRoot
$Exe = Join-Path $Root "app\out\build\win-amd64-relwithdebinfo\bioshock.exe"
$Cfg = Join-Path $Root "run\launcher.json"
$Looks = @("Pure", "Crisp", "Sharp", "Clean", "Ultra", "Cinematic")
$LookInfo = @{
    Pure = "Default. Zero blur, 2x resolution, tuned brightness."
    Crisp = "Like Pure with a light bloom glow."
    Sharp = "2x, strongest anti-aliasing."
    Clean = "3x supersampling. Needs a strong GPU."
    Ultra = "4x supersampling. Very heavy."
    Cinematic = "Original depth-of-field and bloom, 3x."
}

$s = @{ Look = "Pure"; Fps = "60"; Exposure = ""; Gamma = ""; Scale = "Look default" }
if (Test-Path $Cfg) { try { (Get-Content $Cfg -Raw | ConvertFrom-Json).PSObject.Properties | ForEach-Object { $s[$_.Name] = $_.Value } } catch {} }

function New-Ctl($type, $x, $y, $w, $h, $text) {
    $c = New-Object $type; $c.Location = New-Object Drawing.Point($x, $y); $c.Size = New-Object Drawing.Size($w, $h)
    if ($text) { $c.Text = $text }; $form.Controls.Add($c); $c
}
$form = New-Object Windows.Forms.Form
$form.Text = "BioShock Recomp"; $form.ClientSize = New-Object Drawing.Size(460, 330)
$form.FormBorderStyle = "FixedDialog"; $form.MaximizeBox = $false; $form.StartPosition = "CenterScreen"
$form.BackColor = [Drawing.Color]::FromArgb(18, 28, 24); $form.ForeColor = [Drawing.Color]::FromArgb(200, 230, 210)
$form.Font = New-Object Drawing.Font("Segoe UI", 9.5)

$title = New-Ctl Windows.Forms.Label 16 10 430 30 "BioShock  -  Xbox 360 recomp"
$title.Font = New-Object Drawing.Font("Segoe UI Semibold", 15)

New-Ctl Windows.Forms.Label 16 52 90 22 "Look" | Out-Null
$cbLook = New-Ctl Windows.Forms.ComboBox 110 50 140 24 $null; $cbLook.DropDownStyle = "DropDownList"
$Looks | ForEach-Object { [void]$cbLook.Items.Add($_) }; $cbLook.SelectedItem = $s.Look
$lbInfo = New-Ctl Windows.Forms.Label 110 76 340 20 ""; $lbInfo.ForeColor = [Drawing.Color]::FromArgb(140, 170, 150)
$cbLook.Add_SelectedIndexChanged({ $lbInfo.Text = $LookInfo[$cbLook.SelectedItem] }); $lbInfo.Text = $LookInfo[$cbLook.SelectedItem]

New-Ctl Windows.Forms.Label 16 104 90 22 "Frame rate" | Out-Null
$cbFps = New-Ctl Windows.Forms.ComboBox 110 102 140 24 $null; $cbFps.DropDownStyle = "DropDownList"
@("60", "30 (original)") | ForEach-Object { [void]$cbFps.Items.Add($_) }; $cbFps.SelectedIndex = $(if ($s.Fps -eq "30") { 1 } else { 0 })

New-Ctl Windows.Forms.Label 16 136 90 22 "Resolution" | Out-Null
$cbScale = New-Ctl Windows.Forms.ComboBox 110 134 140 24 $null; $cbScale.DropDownStyle = "DropDownList"
@("Look default", "1 (720p)", "2 (1440p)", "3 (2160p)", "4 (2880p)") | ForEach-Object { [void]$cbScale.Items.Add($_) }
$cbScale.SelectedItem = $(if ($cbScale.Items.Contains($s.Scale)) { $s.Scale } else { "Look default" })

New-Ctl Windows.Forms.Label 16 168 90 22 "Brightness" | Out-Null
$tbExp = New-Ctl Windows.Forms.TextBox 110 166 60 24 $s.Exposure
New-Ctl Windows.Forms.Label 176 168 80 22 "Gamma" | Out-Null
$tbGam = New-Ctl Windows.Forms.TextBox 236 166 60 24 $s.Gamma
$lbHint = New-Ctl Windows.Forms.Label 304 168 150 22 "blank = look default"; $lbHint.ForeColor = $lbInfo.ForeColor

$status = New-Ctl Windows.Forms.Label 16 296 430 22 ""
function Set-Status { if (Test-Path $Exe) { $status.Text = "Ready." ; $status.ForeColor = [Drawing.Color]::FromArgb(120, 220, 140) }
                      else { $status.Text = "Not built yet: run Install.ps1 (or Build + Play)."; $status.ForeColor = [Drawing.Color]::Orange } }
Set-Status

function Save-Settings {
    $s.Look = $cbLook.SelectedItem; $s.Fps = $(if ($cbFps.SelectedIndex -eq 1) { "30" } else { "60" })
    $s.Scale = $cbScale.SelectedItem; $s.Exposure = $tbExp.Text.Trim(); $s.Gamma = $tbGam.Text.Trim()
    New-Item -ItemType Directory (Split-Path $Cfg) -Force | Out-Null
    $s | ConvertTo-Json | Set-Content $Cfg
}
function Get-PlayArgs {
    $a = "-Look $($s.Look)"
    if ($s.Fps -eq "30") { $a += " -Fps30" }
    if ($s.Scale -match "^(\d)") { $a += " -Scale $($Matches[1])" }
    $d = 0.0
    if ($s.Exposure -and [double]::TryParse($s.Exposure, [ref]$d)) { $a += " -Exposure $d" }
    if ($s.Gamma -and [double]::TryParse($s.Gamma, [ref]$d)) { $a += " -Gamma $d" }
    $a
}
function Start-Console([string]$cmd, [switch]$Keep) {
    $flags = @("-NoProfile", "-ExecutionPolicy", "Bypass")
    if ($Keep) { $flags += "-NoExit" }
    Start-Process powershell -WorkingDirectory $Root -ArgumentList ($flags + @("-Command", $cmd))
}

$btnPlay = New-Ctl Windows.Forms.Button 16 206 140 38 "Play"
$btnPlay.Font = New-Object Drawing.Font("Segoe UI Semibold", 11); $btnPlay.BackColor = [Drawing.Color]::FromArgb(40, 120, 60)
$btnPlay.Add_Click({
    Save-Settings
    if (-not (Test-Path $Exe)) { [Windows.Forms.MessageBox]::Show("bioshock.exe isn't built yet. Run Install.ps1 first.", "BioShock Recomp") | Out-Null; return }
    Start-Console "& '.\tools\play.ps1' $(Get-PlayArgs)"
})
$btnBuild = New-Ctl Windows.Forms.Button 164 206 140 38 "Build + Play"
$btnBuild.Add_Click({ Save-Settings; Start-Console ". '.\tools\vsenv.ps1'; & '.\go.ps1' $(Get-PlayArgs)" -Keep })
$btnReport = New-Ctl Windows.Forms.Button 312 206 132 38 "Last run report"
$btnReport.Add_Click({ Start-Console "& '.\tools\report.ps1'" -Keep })
$btnLogs = New-Ctl Windows.Forms.Button 16 252 140 32 "Open logs folder"
$btnLogs.Add_Click({ $l = Join-Path $Root "logs"; New-Item -ItemType Directory $l -Force | Out-Null; Start-Process explorer $l })
$btnFix = New-Ctl Windows.Forms.Button 164 252 140 32 "Crash-fix loop (dev)"
$btnFix.Add_Click({ Start-Console ". '.\tools\vsenv.ps1'; & '.\tools\fix_loop.ps1'" -Keep })
$btnHelp = New-Ctl Windows.Forms.Button 312 252 132 32 "In-game keys"
$btnHelp.Add_Click({ [Windows.Forms.MessageBox]::Show("F9   Testing panel (god mode, noclip, teleport, give weapons/plasmids, spawn, weapon mods)`nF10  Mark a glitch in the log (+ RenderDoc capture when running under RenderDoc)`nF4   Emulator settings`nF3   Debug overlay`n`nTest cheats on a spare save.", "BioShock Recomp") | Out-Null })
foreach ($b in @($btnBuild, $btnReport, $btnLogs, $btnFix, $btnHelp)) { $b.BackColor = [Drawing.Color]::FromArgb(36, 52, 44) }
foreach ($b in @($btnPlay, $btnBuild, $btnReport, $btnLogs, $btnFix, $btnHelp)) { $b.FlatStyle = "Flat"; $b.FlatAppearance.BorderColor = [Drawing.Color]::FromArgb(70, 110, 85) }
foreach ($c in @($cbLook, $cbFps, $cbScale, $tbExp, $tbGam)) { $c.BackColor = [Drawing.Color]::FromArgb(30, 44, 38); $c.ForeColor = $form.ForeColor }

$form.Add_FormClosing({ Save-Settings })
[void]$form.ShowDialog()
