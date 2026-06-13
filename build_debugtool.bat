@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
set "CMAKE=D:\Program Files\CMake\bin\cmake.exe"
set "APP_DIR=%ROOT%\app"
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
"%CMAKE%" --build "%ROOT%\build" --config Debug --target debugTool -j 2
set "EXITCODE=%ERRORLEVEL%"
if not errorlevel 1 (
  if not exist "%APP_DIR%" mkdir "%APP_DIR%"
  if exist "%ROOT%\build\Debug\debugTool.exe" copy /Y "%ROOT%\build\Debug\debugTool.exe" "%APP_DIR%\" >nul
  if exist "%ROOT%\build\Debug\SDL3d.dll" copy /Y "%ROOT%\build\Debug\SDL3d.dll" "%APP_DIR%\" >nul
)
popd
exit /b %EXITCODE%
