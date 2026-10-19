@echo off
setlocal
set "FIX_EXE=%~dp0..\FarCry4_FPSFix.exe"
if not exist "%FIX_EXE%" (
    echo FarCry4_FPSFix.exe was not found: "%FIX_EXE%"
    echo Build the executable and place it in the mod root folder.
    exit /b 1
)
"%FIX_EXE%" --prepare
if errorlevel 1 exit /b %ERRORLEVEL%
start "" /min "%FIX_EXE%" --ubisoft
exit /b 0
