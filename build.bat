@echo off
setlocal EnableDelayedExpansion

:: Save current directory
set "PROJECT_DIR=%CD%"

:: Setup Visual Studio environment
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

:: Return to project directory
cd /d "%PROJECT_DIR%"

:: Create build directory
if not exist build mkdir build

echo.
echo Compiling...
echo.

:: Compile
cl.exe /std:c++17 /EHsc /Fe:build\AISubscriptionMonitor.exe /nologo ^
    /I"src" ^
    src\main.cpp ^
    src\subscription.cpp ^
    src\http_client.cpp ^
    src\renderer.cpp ^
    /link ^
    winhttp.lib gdi32.lib user32.lib kernel32.lib comctl32.lib ^
    /SUBSYSTEM:WINDOWS

if %ERRORLEVEL% == 0 (
    echo.
    echo ========================================
    echo Build successful!
    echo Executable: build\AISubscriptionMonitor.exe
    echo ========================================
) else (
    echo.
    echo ========================================
    echo Build failed! Error code: %ERRORLEVEL%
    echo ========================================
)

endlocal
