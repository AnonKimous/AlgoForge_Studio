@echo off
setlocal EnableExtensions

set "ROOT=%~dp0.."
set "DEBUG_TOOL=%ROOT%\build\Debug\debugTool.exe"
set "ORIG_PATH=%Path%"
set "PATH="
set "Path=%ORIG_PATH%"

if not exist "%DEBUG_TOOL%" (
  pushd "%ROOT%"
  call "%ROOT%\build_debugtool.bat"
  popd
  if errorlevel 1 exit /b 1
)

if not exist "%DEBUG_TOOL%" exit /b 1

pushd "%ROOT%"
start "" "%DEBUG_TOOL%"
popd
exit /b 0
