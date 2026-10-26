@echo off
rem Build MARE LIBERUM with Visual Studio (cl). Run from anywhere.
rem   game\build.bat          -> mare.exe (release)
rem   game\build.bat debug    -> mare.exe with symbols, no optimisation
rem   game\build.bat test     -> diag_test.exe + sim_test.exe + asset_test.exe (headless) and runs them
cd /d "%~dp0\.."
where /q cl.exe || call "%ProgramFiles%/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvarsx86_amd64.bat" >nul 2>nul
where /q cl.exe || call "%ProgramFiles%/Microsoft Visual Studio/2022/Professional/VC/Auxiliary/Build/vcvarsx86_amd64.bat" >nul 2>nul
where /q cl.exe || call "%ProgramFiles%/Microsoft Visual Studio/2022/Enterprise/VC/Auxiliary/Build/vcvarsx86_amd64.bat" >nul 2>nul
where /q cl.exe || call "%ProgramFiles(x86)%/Microsoft Visual Studio/2019/Community/VC/Auxiliary/Build/vcvarsx86_amd64.bat" >nul 2>nul
where /q cl.exe || ( echo cl.exe not found: install Visual Studio 2019+ with the "Desktop development with C++" workload && exit /b 1 )

rem Two translation units: the C11 unity build (mare.c) and the job system's C++17 unit
rem (src\core\jobs.cpp, see game\docs\DESIGN_JOBS.md). cl refuses /std:c11 and /std:c++17 on one
rem command line (D8016), so the C++ unit is compiled on its own and handed to the link step.
set COMMON=/nologo /utf-8 /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX /DWIN32_LEAN_AND_MEAN /wd4244 /wd4305
set LANG_C=/std:c11
set LANG_CXX=/std:c++17 /EHsc
if "%1"=="test" (
    cl %COMMON% %LANG_C% /O2 game\tests\diag_test.c /Fe:diag_test.exe || exit /b 1
    diag_test.exe || exit /b 1
    cl %COMMON% %LANG_CXX% /O2 /MT game\tests\jobs_test.cpp /Fe:jobs_test.exe || exit /b 1
    jobs_test.exe || exit /b 1
    cl %COMMON% %LANG_CXX% /O2 /MT /c game\src\core\jobs.cpp /Fo:jobs_t.obj || exit /b 1
    cl %COMMON% %LANG_C% /O2 /MT game\tests\sim_test.c jobs_t.obj /Fe:sim_test.exe || exit /b 1
    del sim_test.obj jobs_t.obj >nul 2>nul
    sim_test.exe 2
    if errorlevel 1 exit /b 1
    cl %COMMON% %LANG_C% /O2 game\tests\asset_test.c /Fe:asset_test.exe || exit /b 1
    asset_test.exe || exit /b 1
    exit /b 0
)
set FLAGS=/O2 /DNDEBUG=1 /MT
if "%1"=="debug" set FLAGS=/Zi /Od /MTd
cl %COMMON% %LANG_CXX% %FLAGS% /c game\src\core\jobs.cpp /Fo:jobs.obj || exit /b 1
cl %COMMON% %LANG_C% /MP %FLAGS% /I. game\mare.c jobs.obj /Fe:mare.exe || exit /b 1
del mare.obj jobs.obj >nul 2>nul
echo done: mare.exe  (logs are written to .\logs\ next to the executable)
