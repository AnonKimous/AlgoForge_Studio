@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
set "CMAKE=D:\Program Files\CMake\bin\cmake.exe"
set "ORIG_PATH=%Path%"
set "PATH="
set "Path=%ORIG_PATH%"

pushd "%ROOT%"
"%CMAKE%" -S "%ROOT%" -B "%ROOT%\build" --fresh -DBUILD_DEBUG_TOOL_SAMPLE_PLUGIN=OFF
if errorlevel 1 (
  set "EXITCODE=%ERRORLEVEL%"
  popd
  exit /b %EXITCODE%
)
"%CMAKE%" --build "%ROOT%\build" --config Debug --target sdk -j 2
set "EXITCODE=%ERRORLEVEL%"
popd
exit /b %EXITCODE%
