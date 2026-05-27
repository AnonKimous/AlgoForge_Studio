@echo off
setlocal

set "ROOT=%~dp0"
set "DEBUG_EXE=%ROOT%build\Debug\min_vulkan_win32.exe"
set "RELEASE_EXE=%ROOT%build\Release\min_vulkan_win32.exe"

if exist "%DEBUG_EXE%" (
  start "" "%DEBUG_EXE%" validationlayer:on
  exit /b 0
)

if exist "%RELEASE_EXE%" (
  start "" "%RELEASE_EXE%" validationlayer:on
  exit /b 0
)

echo Could not find min_vulkan_win32.exe in build\Debug or build\Release.
pause
