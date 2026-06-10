@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
set "CMAKE=D:\Program Files\CMake\bin\cmake.exe"
set "SDK_LIB=%ROOT%\sdk\sdk.lib"
set "SDK_HEADER=%ROOT%\sdk\include\sdk_kernel.h"
set "ALGO_BUILD=%ROOT%\build_algorithms"
set "ORIG_PATH=%Path%"
set "PATH="
set "Path=%ORIG_PATH%"

if not exist "%SDK_LIB%" (
  echo SDK build artifact is missing: "%SDK_LIB%"
  exit /b 1
)

if not exist "%SDK_HEADER%" (
  echo SDK public header is missing: "%SDK_HEADER%"
  exit /b 1
)

pushd "%ROOT%"
"%CMAKE%" -S "%ROOT%" -B "%ALGO_BUILD%" --fresh -DBUILD_DEBUG_TOOL_SAMPLE_PLUGIN=ON
if errorlevel 1 (
  set "EXITCODE=%ERRORLEVEL%"
  popd
  exit /b %EXITCODE%
)
"%CMAKE%" --build "%ALGO_BUILD%" --config Debug --target temporary_test_line_motion_plugin -j 2
set "EXITCODE=%ERRORLEVEL%"
popd
exit /b %EXITCODE%
