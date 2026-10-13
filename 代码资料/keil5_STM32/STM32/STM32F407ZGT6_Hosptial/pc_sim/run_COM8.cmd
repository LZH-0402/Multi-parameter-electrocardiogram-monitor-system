@echo off
setlocal
cd /d "%~dp0"
if not exist "build\SPO2_PCSim.exe" (
    echo Build the program first using build.cmd.
    pause
    exit /b 1
)
"build\SPO2_PCSim.exe" COM8
pause