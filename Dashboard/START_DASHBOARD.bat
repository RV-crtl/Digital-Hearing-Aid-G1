@echo off
setlocal EnableExtensions
cd /d "%~dp0"
rem Install only inside Dashboard\.venv; never mutate global Python packages.
where python >nul 2>nul || (
  echo Python 3 not found on PATH. Install Python 3.12 and retry.
  pause
  exit /b 1
)
if not exist ".venv\Scripts\python.exe" (
  echo Creating isolated dashboard Python environment...
  python -m venv ".venv"
  if errorlevel 1 goto :fail
)
set "APP_PY=.venv\Scripts\python.exe"
"%APP_PY%" -m pip --disable-pip-version-check install -r requirements.txt
if errorlevel 1 goto :fail
"%APP_PY%" connectivity_dashboard.py
if errorlevel 1 goto :fail
exit /b 0
:fail
echo Dashboard installation or startup failed. Check the message above.
pause
exit /b 1
