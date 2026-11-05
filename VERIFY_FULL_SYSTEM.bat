@echo off
cd /d "%~dp0"
where py >nul 2>nul
if %ERRORLEVEL% EQU 0 (
  py -3 Tools\verify_active_project.py || exit /b 1
  py -3 Tools\verify_build_bundle.py --verify-backup || exit /b 1
  py -3 Tools\g1_preflight.py || exit /b 1
  py -3 CHECK_PROJECT.py || exit /b 1
  py -3 CHECK_GROUP.py || exit /b 1
) else (
  python Tools\verify_active_project.py || exit /b 1
  python Tools\verify_build_bundle.py --verify-backup || exit /b 1
  python Tools\g1_preflight.py || exit /b 1
  python CHECK_PROJECT.py || exit /b 1
  python CHECK_GROUP.py || exit /b 1
)
echo SOURCE PREFLIGHT PASSED. Now Clean and Build in STM32CubeIDE to validate ARM toolchain.
pause
