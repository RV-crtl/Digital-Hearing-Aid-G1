@echo off
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0VERIFY_HAL_OBJECT_SYMBOLS.ps1"
set RC=%ERRORLEVEL%
pause
exit /b %RC%
