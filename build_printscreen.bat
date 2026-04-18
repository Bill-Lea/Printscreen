@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM ===== Settings you might tweak ============================================
set "TARGET=Printscreen"                     REM <- your CMake target / DLL name
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=debug"         REM default preset
REM If you want to force-set MO2 path per-run, pass it as 2nd arg.
if not "%~2"=="" set "SKYRIM_MODS_FOLDER=%~2"
REM ===========================================================================

REM Repo root = this .bat’s folder
set "REPO=%~dp0"
set "LOGDIR=%REPO%build_logs"
if not exist "%LOGDIR%" mkdir "%LOGDIR%"

REM Make a safe timestamp for log filename
for /f "tokens=1-3 delims=/ " %%a in ("%date%") do (set "Y=%%c" & set "M=%%a" & set "D=%%b")
set "T=%time::=-%"
set "T=%T: =0%"
set "LOGFILE=%LOGDIR%\%TARGET%_%CONFIG%_%Y%-%M%-%D%_%T%.log"

echo.
echo === Build script for %TARGET% (%CONFIG%) ===
echo Repo: %REPO%
echo Log : %LOGFILE%
if defined SKYRIM_MODS_FOLDER echo MO2 : %SKYRIM_MODS_FOLDER%
echo.

REM Ensure VS toolchain is available (so cl.exe is on PATH)
where cl.exe >NUL 2>&1
if errorlevel 1 (
  echo [INFO] Initializing Visual Studio build environment...
  if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    for /f "usebackq tokens=*" %%I in (`
      "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * ^
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    `) do set "VSINSTALL=%%I"
    if defined VSINSTALL (
      call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 >nul
    ) else (
      call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 >nul
    )
  ) else (
    call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 >nul
  )
)

pushd "%REPO%" || (echo [ERROR] Repo not found: %REPO% & pause & exit /b 1)

echo.
echo === CONFIGURE (cmake --preset %CONFIG%) ===
powershell -NoProfile -Command ^
  "cmake --preset %CONFIG% 2>&1 | Tee-Object -FilePath '%LOGFILE%' -Append; exit $LASTEXITCODE"
if errorlevel 1 (
  echo.
  echo [FAIL] Configure step failed. See log: "%LOGFILE%"
  popd & pause & exit /b 1
)

echo.
echo === BUILD (cmake --build --preset %CONFIG% -v) ===
powershell -NoProfile -Command ^
  "cmake --build --preset %CONFIG% -v 2>&1 | Tee-Object -FilePath '%LOGFILE%' -Append; exit $LASTEXITCODE"
if errorlevel 1 (
  echo.
  echo [FAIL] Build step failed. See log: "%LOGFILE%"
  popd & pause & exit /b 1
)

echo.
echo === VERIFY ARTIFACT ===
set "DLL_FOUND="
REM If MO2 path given, check there first (matches your CMake POST_BUILD copy)
if defined SKYRIM_MODS_FOLDER (
  set "MO2_DLL=%SKYRIM_MODS_FOLDER%\Printscreen\SKSE\Plugins\%TARGET%.dll"
  if exist "%MO2_DLL%" (
    set "DLL_FOUND=%MO2_DLL%"
    echo [OK] Found DLL in MO2: "%MO2_DLL%"
    for %%Z in ("%MO2_DLL%") do echo      Size: %%~zZ bytes  Modified: %%~tZ
  ) else (
    echo [WARN] DLL not found in MO2 path: "%MO2_DLL%"
  )
)

REM If not found in MO2, try to locate newest DLL under the repo (build dir)
if not defined DLL_FOUND (
  for /f "delims=" %%F in ('dir /b /s /a:-d /o:-d "%REPO%\%TARGET%.dll" 2^>nul') do (
    if not defined DLL_FOUND set "DLL_FOUND=%%F"
  )
  if defined DLL_FOUND (
    echo [OK] Found DLL in build tree: "%DLL_FOUND%"
    for %%Z in ("%DLL_FOUND%") do echo      Size: %%~zZ bytes  Modified: %%~tZ
  ) else (
    echo [WARN] Could not locate %TARGET%.dll in repo. Build likely succeeded, but post-build copy or output path differs.
  )
)

echo.
echo [SUCCESS] Configure + Build completed. Log saved to:
echo          "%LOGFILE%"
echo.

popd
pause
endlocal
