@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Launch-Install-MagDrum.ps1"
if errorlevel 1 (
    echo.
    echo Installation did not complete. Review the message above; if UAC was cancelled, run this file again and approve the prompt.
    pause
    exit /b 1
)
exit /b 0
