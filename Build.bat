@echo off
setlocal
cd /d "%~dp0"
for %%I in ("%~dp0game") do set "SOURCE_DIR=%%~fI"
set "SOURCE_DIR=%SOURCE_DIR:\=/%"
if exist "build\CMakeCache.txt" (
    findstr /I /L /X /C:"CMAKE_HOME_DIRECTORY:INTERNAL=%SOURCE_DIR%" "build\CMakeCache.txt" >nul
    if errorlevel 1 (
        echo Recreating stale CMake cache for %SOURCE_DIR%...
        cmake -E rm -rf "build\CMakeCache.txt" "build\CMakeFiles" || exit /b 1
    )
)
cmake -S game -B build || exit /b 1
cmake --build build --config Release --parallel || exit /b 1
if /I "%~1"=="test" (
    ctest --test-dir build -C Release --output-on-failure
    if errorlevel 1 exit /b 1
)
exit /b 0
