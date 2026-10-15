@echo off
setlocal
cd /d "%~dp0"
where python >nul 2>nul || (echo Python not found.& pause & exit /b 1)
python -m pip install -q -r requirements.txt
python connectivity_dashboard.py
if errorlevel 1 pause
