@echo off
setlocal
cd /d "%~dp0"
if not exist "APL_V3_1_INSTALL_AND_RUN.py" (
  echo ERROR: APL_V3_1_INSTALL_AND_RUN.py must be in the same folder as this launcher.
  pause
  exit /b 2
)
py -3.11 "%~dp0APL_V3_1_INSTALL_AND_RUN.py" %*
set RC=%ERRORLEVEL%
if not "%RC%"=="0" pause
exit /b %RC%
