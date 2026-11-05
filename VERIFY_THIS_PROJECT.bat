@echo off
cd /d "%~dp0"
where py >nul 2>nul
if %ERRORLEVEL% EQU 0 (py -3 Tools\verify_active_project.py) else (python Tools\verify_active_project.py)
pause
