@echo off
setlocal
cd /d "%~dp0"
call PREPARE_PROJECT.bat
if errorlevel 1 exit /b %errorlevel%
call VERIFY_FINAL.bat
exit /b %errorlevel%
