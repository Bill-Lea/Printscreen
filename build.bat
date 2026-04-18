@echo off
REM PrintScreen SKSE Plugin Build Script
REM Usage: build.bat [Release|Debug] [clean]

setlocal

set CONFIG=Release
set CLEAN=

:parse_args
if "%~1"=="" goto run
if /i "%~1"=="debug" set CONFIG=Debug
if /i "%~1"=="release" set CONFIG=Release
if /i "%~1"=="clean" set CLEAN=-Clean
shift
goto parse_args

:run
echo === PrintScreen Build Script ===
echo Configuration: %CONFIG%

powershell -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Config %CONFIG% %CLEAN%

if %ERRORLEVEL% neq 0 (
    echo Build failed!
    pause
    exit /b 1
)

echo.
pause
