@echo off
rem Build MARE LIBERUM with Visual Studio (cl). Run from anywhere.
rem   game\build.bat          -> mare.exe (release)
rem   game\build.bat debug    -> mare.exe with symbols, no optimisation
rem   game\build.bat test     -> diag_test.exe + sim_test.exe (headless) and runs them
cd /d "%~dp0\.."
where /q cl.exe || call "%ProgramFiles%/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvarsx86_amd64.bat" >nul 2>nul
where /q cl.exe || call "%ProgramFiles%/Microsoft Visual Studio/2022/Professional/VC/Auxiliary/Build/vcvarsx86_amd64.bat" >nul 2>nul
where /q cl.exe || call "%ProgramFiles%/Microsoft Visual Studio/2022/Enterprise/VC/Auxiliary/Build/vcvarsx86_amd64.bat" >nul 2>nul
where /q cl.exe || call "%ProgramFiles(x86)%/Microsoft Visual Studio/2019/Community/VC/Auxiliary/Build/vcvarsx86_amd64.bat" >nul 2>nul
where /q cl.exe || ( echo cl.exe not found: install Visual Studio 2019+ with the "Desktop development with C++" workload && exit /b 1 )

set COMMON=/nologo /std:c11 /utf-8 /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX /DWIN32_LEAN_AND_MEAN /wd4244 /wd4305
if "%1"=="test" (
    cl %COMMON% /O2 game\tests\diag_test.c /Fe:diag_test.exe || exit /b 1
    diag_test.exe || exit /b 1
    cl %COMMON% /O2 game\tests\sim_test.c /Fe:sim_test.exe || exit /b 1
    sim_test.exe 2
    if errorlevel 1 exit /b 1
    exit /b 0
)
set FLAGS=/O2 /DNDEBUG=1 /MT
if "%1"=="debug" set FLAGS=/Zi /Od /MTd
cl %COMMON% /MP %FLAGS% /I. game\mare.c /Fe:mare.exe || exit /b 1
del mare.obj >nul 2>nul
echo done: mare.exe  (logs are written to .\logs\ next to the executable)
