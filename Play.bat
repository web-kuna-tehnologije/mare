@echo off
setlocal
cd /d "%~dp0"
if not exist "build\Release\mare.exe" (
    call Build.bat
    if errorlevel 1 (
        echo Build failed. Install Visual Studio C++ tools and CMake, then try again.
        pause
        exit /b 1
    )
)
start "Mare Liberum" /D "%~dp0build\Release" "%~dp0build\Release\mare.exe" %*
