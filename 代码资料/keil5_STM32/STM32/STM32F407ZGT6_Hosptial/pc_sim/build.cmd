@echo off
setlocal
cd /d "%~dp0"
call "F:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -no_logo -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1
cl /nologo /TC /std:c11 /Od /Zi /W3 /D_CRT_SECURE_NO_WARNINGS /source-charset:.936 /execution-charset:.936 /I"build\src" "build\src\host_main.c" "build\src\dispatch.c" "build\src\app_spo2.c" "build\src\meas.c" "build\src\hmi_driver.c" "build\src\cmd_queue.c" /Fo"build\\" /Fd"build\SPO2_PCSim.pdb" /Fe"build\SPO2_PCSim.exe" /link /DEBUG
exit /b %errorlevel%