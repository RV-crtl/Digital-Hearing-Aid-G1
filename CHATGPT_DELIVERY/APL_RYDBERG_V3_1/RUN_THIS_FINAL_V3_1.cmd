@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

if not exist "02_FULL_STOKES_FINAL\gen2_hfss.py" goto :wrongfolder
if not exist "02_FULL_STOKES_FINAL\analyze_atomic_convergence.py" goto :wrongfolder
if not exist "02_FULL_STOKES_FINAL\design_config_gen2.json" goto :wrongfolder
if not exist "02_FULL_STOKES_FINAL\v31_resume_and_verdict.py" goto :missingpatch
if not exist "02_FULL_STOKES_FINAL\v31_record_failure.py" goto :missingpatch
if not exist "02_FULL_STOKES_FINAL\pack_v31_return.py" goto :missingpatch

cd /d "%~dp0\02_FULL_STOKES_FINAL"

echo ============================================================
echo APL RYDBERG FINAL CONVERGENCE V3.1 - RESUME + VERDICT
echo ============================================================

call 00_SETUP_ENV.cmd
if errorlevel 1 (
  set FAILRC=!ERRORLEVEL!
  set FAILSTAGE=ENVIRONMENT_SETUP
  goto :recordfail
)

call .venv\Scripts\activate.bat

python -u febi_hybrid_self_test.py
if errorlevel 1 (
  set FAILRC=!ERRORLEVEL!
  set FAILSTAGE=FEBI_HYBRID_SELF_TEST
  goto :recordfail
)

python -u v31_resume_and_verdict.py
set RC=!ERRORLEVEL!

python -u pack_v31_return.py
goto :report

:recordfail
if exist ".venv\Scripts\python.exe" (
  ".venv\Scripts\python.exe" v31_record_failure.py "!FAILSTAGE!" "!FAILRC!"
  ".venv\Scripts\python.exe" pack_v31_return.py
) else (
  py -3.11 v31_record_failure.py "!FAILSTAGE!" "!FAILRC!"
  py -3.11 pack_v31_return.py
)
set RC=!FAILRC!
goto :report

:wrongfolder
echo.
echo ERROR: RUN_THIS_FINAL_V3_1.cmd is not in the extracted V3 package root.
echo It must sit next to the folder:
echo   02_FULL_STOKES_FINAL
echo.
pause
exit /b 9

:missingpatch
echo.
echo ERROR: One or more V3.1 Python patch files are missing from:
echo   02_FULL_STOKES_FINAL
echo Required:
echo   v31_resume_and_verdict.py
echo   v31_record_failure.py
echo   pack_v31_return.py
echo.
pause
exit /b 8

:report
cd /d "%~dp0"
echo.
echo ============================================================
echo V3.1 FINISHED WITH RETURN CODE !RC!
echo ============================================================
if exist FINAL_PAPER_VERDICT.txt type FINAL_PAPER_VERDICT.txt
echo.
echo UPLOAD THESE THREE FILES BACK TO CHATGPT:
echo   1. newest RETURN_TO_CHATGPT_APL_V3_1_*.zip
echo   2. FINAL_PAPER_VERDICT.json
echo   3. RUN_STATUS.json
echo.
dir /b /o-d RETURN_TO_CHATGPT_APL_V3_1_*.zip 2>nul
if not "!RC!"=="0" pause
exit /b !RC!
