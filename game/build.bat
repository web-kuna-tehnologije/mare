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

rem Translation units: the C11 unity build of the unported modules (mare.c) and the C++20 core
rem (src\core\diag.cc + src\core\worker_pool.cc, see docs\REWRITE_PLAN.md). cl refuses /std:c11
rem and /std:c++20 on one command line (D8016), so the C++ units compile on their own and are
rem handed to the link step.
set COMMON=/nologo /utf-8 /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX /DWIN32_LEAN_AND_MEAN /wd4244 /wd4305
set LANG_C=/std:c11
set LANG_CXX=/std:c++20 /EHsc
if "%1"=="test" (
    cl %COMMON% %LANG_CXX% /O2 /MT /c game\src\core\diag.cc /Fo:diag_t.obj || exit /b 1
    cl %COMMON% %LANG_CXX% /O2 /MT /c game\src\core\worker_pool.cc /Fo:jobs_t.obj || exit /b 1
    cl %COMMON% %LANG_C% /O2 /MT game\tests\diag_test.c diag_t.obj /Fe:diag_test.exe || exit /b 1
    diag_test.exe || exit /b 1
    cl %COMMON% %LANG_CXX% /O2 /MT game\tests\jobs_test.cpp /Fe:jobs_test.exe || exit /b 1
    jobs_test.exe || exit /b 1
    cl %COMMON% %LANG_C% /O2 /MT game\tests\sim_test.c diag_t.obj jobs_t.obj /Fe:sim_test.exe || exit /b 1
    sim_test.exe 2
    if errorlevel 1 exit /b 1
    cl %COMMON% %LANG_C% /O2 /MT game\tests\asset_test.c diag_t.obj /Fe:asset_test.exe || exit /b 1
    asset_test.exe || exit /b 1
    del sim_test.obj asset_test.obj diag_test.obj diag_t.obj jobs_t.obj >nul 2>nul
    exit /b 0
)
set FLAGS=/O2 /DNDEBUG=1 /MT
if "%1"=="debug" set FLAGS=/Zi /Od /MTd
cl %COMMON% %LANG_CXX% %FLAGS% /c game\src\core\diag.cc /Fo:diag.obj || exit /b 1
cl %COMMON% %LANG_CXX% %FLAGS% /c game\src\core\worker_pool.cc /Fo:jobs.obj || exit /b 1
cl %COMMON% %LANG_C% /MP %FLAGS% /I. game\mare.c diag.obj jobs.obj /Fe:mare.exe || exit /b 1
del mare.obj diag.obj jobs.obj >nul 2>nul
echo done: mare.exe  (logs are written to .\logs\ next to the executable)
