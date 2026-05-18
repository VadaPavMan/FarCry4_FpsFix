@echo off
REM launch options:
REM   "C:\FarCry4-FpsFixMod\SteamLauncher.bat" && %%command%%


start "" /MIN powershell -ExecutionPolicy Bypass -NoProfile -WindowStyle Hidden -File "%~dp0launcher\process.ps1" -ConfigPath "%~dp0config\settings.json"

exit /B 0
