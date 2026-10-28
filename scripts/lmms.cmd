@echo off
rem Build helper for LMMS + KeyFall on Windows (MSVC + vcpkg + Qt6).
rem Usage: scripts\lmms.cmd configure ^| build ^| run
setlocal

if "%LMMS_SRC%"==""  set "LMMS_SRC=%USERPROFILE%\Projects\lmms"
if "%LMMS_BUILD%"=="" set "LMMS_BUILD=%LMMS_SRC%\build"
if "%KEYFALL_VCPKG%"=="" set "KEYFALL_VCPKG=C:\vcpkg"
if "%QT_DIR%"==""    set "QT_DIR=C:\Qt\6.8.3\msvc2022_64"
set "VS_DIR=C:\Program Files\Microsoft Visual Studio\2022\Community"

set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%VS_DIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
where cl >nul 2>&1 || (echo MSVC environment not initialised & exit /b 1)
rem vcvars points VCPKG_ROOT at the VS-bundled vcpkg; LMMS needs the classic clone.
set "VCPKG_ROOT=%KEYFALL_VCPKG%"
set "PATH=%VS_DIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VS_DIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%QT_DIR%\bin;%PATH%"

if /i "%1"=="configure" goto configure
if /i "%1"=="build"     goto build
if /i "%1"=="run"       goto run
if /i "%1"=="test"      goto test
echo Usage: %~nx0 configure ^| build ^| run ^| test
exit /b 1

:test
rem Qt-free KeyFall core: unit tests, layout corpus and built-in layouts (no LMMS needed)
cmake -S "%~dp0..\tests" -B "%~dp0..\build-tests" -G Ninja -DCMAKE_BUILD_TYPE=Debug || exit /b 1
cmake --build "%~dp0..\build-tests" || exit /b 1
ctest --test-dir "%~dp0..\build-tests" --output-on-failure
exit /b %ERRORLEVEL%

:configure
rem The plugin source lives in key-fall\plugin; LMMS sees it as plugins\KeyFall through a junction.
if not exist "%LMMS_SRC%\plugins\KeyFall" mklink /J "%LMMS_SRC%\plugins\KeyFall" "%~dp0..\plugin" || exit /b 1
rem Minimal plug-in set keeps rebuilds fast; drop LMMS_MINIMAL/PLUGIN_LIST for a full build.
cmake -S "%LMMS_SRC%" -B "%LMMS_BUILD%" -G Ninja ^
  --toolchain "%KEYFALL_VCPKG%\scripts\buildsystems\vcpkg.cmake" ^
  -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
  -DCMAKE_PREFIX_PATH="%QT_DIR%" ^
  -DWANT_QT6=ON ^
  -DVCPKG_TARGET_TRIPLET=x64-windows ^
  -DVCPKG_HOST_TRIPLET=x64-windows ^
  -DLMMS_MINIMAL=ON ^
  -DPLUGIN_LIST="KeyFall TapTempo MidiImport"
exit /b %ERRORLEVEL%

:build
cmake --build "%LMMS_BUILD%"
exit /b %ERRORLEVEL%

:run
set "PATH=%LMMS_BUILD%\vcpkg_installed\x64-windows\bin;%PATH%"
rem Separate config so the dev build never touches the installed LMMS's ~/.lmmsrc.xml
if not exist "%~dp0..\dev\lmmsrc-dev.xml" copy "%~dp0..\dev\lmmsrc-dev.template.xml" "%~dp0..\dev\lmmsrc-dev.xml" >nul
start "" /D "%LMMS_BUILD%" "%LMMS_BUILD%\lmms.exe" --config "%~dp0..\dev\lmmsrc-dev.xml"
exit /b 0
