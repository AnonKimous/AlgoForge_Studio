@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
set "CMAKE=cmake"
set "ORIG_PATH=%Path%"
set "PATH="
set "Path=%ORIG_PATH%"

pushd "%ROOT%"
"%CMAKE%" -S "%ROOT%" -B "%ROOT%\build" --fresh
if errorlevel 1 (
  set "EXITCODE=%ERRORLEVEL%"
  popd
  exit /b %EXITCODE%
)
"%CMAKE%" --build "%ROOT%\build" --config Debug --target sdk --parallel
set "EXITCODE=%ERRORLEVEL%"
popd
exit /b %EXITCODE%
