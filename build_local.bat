@echo off
setlocal EnableExtensions

rem Start from a clean copy of PATH/Path for MSBuild.
set "ORIG_PATH=%Path%"
set "PATH="
set "Path=%ORIG_PATH%"

pushd "%~dp0"
"C:\Program Files\CMake\bin\cmake.exe" --build build --config Debug --target min_vulkan_win32 -j 2
set "EXITCODE=%ERRORLEVEL%"
popd
exit /b %EXITCODE%
