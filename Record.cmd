@echo off
rem Record a session: starts OBS recording, starts the game, cuts highlight clips when you quit. See tools\record.ps1.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\record.ps1" %*
pause
