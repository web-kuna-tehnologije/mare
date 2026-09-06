@echo off
setlocal
cd /d "%~dp0"
cmake -S game -B build || exit /b 1
cmake --build build --config Release --parallel || exit /b 1
if /I "%~1"=="test" (
    ctest --test-dir build -C Release --output-on-failure
    if errorlevel 1 exit /b 1
)
exit /b 0
