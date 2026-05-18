@echo off
title FarCry4-FpsFixMod Optimizer

echo.
echo  FarCry4-FpsFixMod is now running in this window.
echo  Launch Far Cry 4 normally through Steam or Ubisoft Connect.
echo  The optimizer will detect the game and apply settings.
echo  This window will close when the game exits.
echo.

powershell -ExecutionPolicy Bypass -NoProfile -File "%~dp0launcher\process.ps1" -ConfigPath "%~dp0config\settings.json"

echo.
echo  Session ended.
pause
