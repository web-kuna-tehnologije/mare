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
rem The Vulkan backend compiles against the real headers when the SDK is installed; without
rem them gfx/rhi_vk.cc builds a stub that reports the backend as unavailable (no link
rem dependency either way - the loader is opened at run time).
if defined VULKAN_SDK (
    if exist "%VULKAN_SDK%\Include\vulkan\vulkan.h" set COMMON=%COMMON% /I"%VULKAN_SDK%\Include"
) else (
    for /d %%v in (C:\VulkanSDK\*) do if exist "%%v\Include\vulkan\vulkan.h" set COMMON=%COMMON% /I"%%v\Include"
)
set LANG_C=/std:c11
set LANG_CXX=/std:c++20 /EHsc
if "%1"=="test" (
    cl %COMMON% %LANG_CXX% /O2 /MT /c game\src\core\diag.cc /Fo:diag_t.obj || exit /b 1
    cl %COMMON% %LANG_CXX% /O2 /MT /c game\src\core\worker_pool.cc /Fo:jobs_t.obj || exit /b 1
    cl %COMMON% %LANG_C% /O2 /MT game\tests\diag_test.c diag_t.obj /Fe:diag_test.exe || exit /b 1
    diag_test.exe || exit /b 1
    cl %COMMON% %LANG_CXX% /O2 /MT game\tests\jobs_test.cpp /Fe:jobs_test.exe || exit /b 1
    jobs_test.exe || exit /b 1
    cl %COMMON% %LANG_CXX% /O2 /MT game\tests\sim_test.cpp diag_t.obj jobs_t.obj /Fe:sim_test.exe || exit /b 1
    sim_test.exe 2
    if errorlevel 1 exit /b 1
    cl %COMMON% %LANG_CXX% /O2 /MT game\tests\asset_test.cpp diag_t.obj /Fe:asset_test.exe || exit /b 1
    asset_test.exe || exit /b 1
    cl %COMMON% %LANG_CXX% /O2 /MT game\tests\entity_test.cpp game\src\engine\entity_manager.cc diag_t.obj jobs_t.obj /Fe:entity_test.exe || exit /b 1
    entity_test.exe || exit /b 1
    del sim_test.obj asset_test.obj diag_test.obj diag_t.obj jobs_t.obj >nul 2>nul
    exit /b 0
)
set FLAGS=/O2 /DNDEBUG=1 /MT
if "%1"=="debug" set FLAGS=/Zi /Od /MTd
rem Every module under game\src compiles as its own C++20 TU (docs/CPP_CONVERSION.md); the
rem C11 unity build (mare.c) holds the v3 framework and whatever is not converted yet.
if exist ccobj rmdir /s /q ccobj
mkdir ccobj
for %%d in (core engine game sim world gfx ui assets data platform) do (
    if exist game\src\%%d\*.cc (
        cl %COMMON% %LANG_CXX% /MP %FLAGS% /I. /Igame /c /Foccobj\ game\src\%%d\*.cc || exit /b 1
    )
)
cl %COMMON% %LANG_C% /MP %FLAGS% /I. /c /Foccobj\ game\src\platform\v3_impl.c || exit /b 1
cl %COMMON% %LANG_CXX% /MP %FLAGS% /I. /Igame /c /Foccobj\ game\src\main.cc || exit /b 1
cl /nologo %FLAGS% ccobj\*.obj /Fe:mare.exe || exit /b 1
rem every successful build is archived under tmp\builds\<root>-<datetime>
for %%I in ("%cd%") do set BUILDROOT=%%~nxI
for /f %%t in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd-HHmmss"') do set BUILDSTAMP=%%t
mkdir tmp\builds\%BUILDROOT%-%BUILDSTAMP% 2>nul
copy /y mare.exe tmp\builds\%BUILDROOT%-%BUILDSTAMP%\mare.exe >nul
echo done: mare.exe  (archived to tmp\builds\%BUILDROOT%-%BUILDSTAMP%; logs in .\logs\)
