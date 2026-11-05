@echo off
setlocal
cd /d "%~dp0"
echo G1 R8 serial acceptance. This will change sources and volume, but leaves output MUTED and source OFF.
set /p G1PORT=Enter COM port (example COM7): 
if "%G1PORT%"=="" exit /b 2
where py >nul 2>&1
if %ERRORLEVEL%==0 (py -3 Tools\g1_serial_acceptance.py --port %G1PORT%) else (python Tools\g1_serial_acceptance.py --port %G1PORT%)
set RC=%ERRORLEVEL%
pause
exit /b %RC%
