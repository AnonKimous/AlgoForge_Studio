@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
set "CMAKE=D:\Program Files\CMake\bin\cmake.exe"
set "HOT_BUILD_DIR=%ROOT%\build_algorithms_hot"
set "ORIG_PATH=%Path%"
set "PATH="
set "Path="
set "Path=%ORIG_PATH%"

if "%~1"=="" (
  echo Missing algorithm target name.
  exit /b 1
)

set "ALGO_TARGET=%~1"

pushd "%ROOT%"
"%CMAKE%" -S "%ROOT%\src\capabilities\algorithm_library" -B "%HOT_BUILD_DIR%" --fresh -DBUILD_ALGORITHM_SAMPLE_PLUGIN=ON -DCORE_BUILD_DIR="%ROOT%\build"
if errorlevel 1 (
  set "EXITCODE=%ERRORLEVEL%"
  popd
  exit /b %EXITCODE%
)
"%CMAKE%" --build "%HOT_BUILD_DIR%" --config Debug --target "%ALGO_TARGET%_package" -j 2
set "EXITCODE=%ERRORLEVEL%"
popd
exit /b %EXITCODE%
