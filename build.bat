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

:: Compile resource file
rc.exe /nologo /fo build\app.res src\app.rc

:: Compile
cl.exe /std:c++17 /EHsc /utf-8 /Fe:build\AISubscriptionMonitor.exe /nologo ^
    /I"src" ^
    src\main.cpp ^
    src\subscription.cpp ^
    src\http_client.cpp ^
    src\renderer.cpp ^
    /link ^
    build\app.res ^
    winhttp.lib gdi32.lib user32.lib kernel32.lib comctl32.lib advapi32.lib dwmapi.lib shell32.lib ole32.lib ^
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
