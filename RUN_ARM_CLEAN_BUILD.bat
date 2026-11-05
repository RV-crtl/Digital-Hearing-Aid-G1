@echo off
cd /d "%~dp0"
echo G1 R10 complete-device: preparing dependencies and building in STM32CubeIDE.
echo Output logs will be in Validation\results\g1_arm_build.log
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0G1_BUILD_ARM.ps1"
if errorlevel 1 (
    echo BUILD FAILED. Send the g1_arm_build.log and stderr files from Validation\results\.
    pause
    exit /b 1
)
echo ARM build complete. This does NOT confirm physical PCB operation.
pause
