@echo off
start "" /MIN powershell -ExecutionPolicy Bypass -NoProfile -WindowStyle Hidden -File "%~dp0launcher\process.ps1" -ConfigPath "%~dp0config\settings.json"
timeout /T 2 /NOBREAK >nul
start "" "uplay://launch/9/0"
exit /B 0