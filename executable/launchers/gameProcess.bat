@echo off
setlocal
 title FarCry4-FPSFix V2 Optimizer
set "FIX_EXE=%~dp0..\FarCry4_FPSFix.exe"
if not exist "%FIX_EXE%" (
    echo FarCry4_FPSFix.exe was not found: "%FIX_EXE%"
    echo Build the executable and place it in the mod root folder.
    pause
    exit /b 1
)
echo.
echo FarCry4-FPSFix V2 optimizer is running in this window.
echo Launch Far Cry 4 normally through Steam or Ubisoft Connect.
echo The optimizer will apply the configured settings and log diagnostics.
echo This window will close when the game exits.
echo.
"%FIX_EXE%"
echo.
echo Optimizer session ended.
pause
