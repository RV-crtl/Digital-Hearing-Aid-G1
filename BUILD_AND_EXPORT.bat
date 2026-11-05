@echo off
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0BUILD_AND_EXPORT.ps1"
set RC=%ERRORLEVEL%
echo Build diagnostics are in Validation\results\SEND_BUILD_TO_CHATGPT_*.zip
pause
exit /b %RC%
