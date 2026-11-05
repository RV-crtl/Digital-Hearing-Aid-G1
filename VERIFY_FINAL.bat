@echo off
setlocal
cd /d "%~dp0"
where py >nul 2>nul
if not errorlevel 1 (py -3 Tools\verify_261104_release.py) else (python Tools\verify_261104_release.py)
if errorlevel 1 (
 echo Verification FAILED. Do not flash.
 pause
 exit /b 1
)
echo Source checks passed. Still perform CubeIDE Clean ^> Build and PCB test.
exit /b 0
