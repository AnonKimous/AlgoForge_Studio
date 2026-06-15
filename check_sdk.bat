@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
set "CMAKE=D:\Program Files\CMake\bin\cmake.exe"
set "SDK_LIB=%ROOT%\sdk\sdk.lib"
set "SDK_HEADER=%ROOT%\sdk\include\sdk_kernel.h"
set "CORE_BUILD_DEBUG=%ROOT%\build\Debug"
set "ALGO_BUILD=%ROOT%\build_algorithms"
set "SRC_LIBRARY_DIR=%ROOT%\src\capabilities\algorithm_library"
set "APP_DIR=%ROOT%\app"
set "APP_LIBRARY_DIR=%APP_DIR%\src\capabilities\algorithm_library"
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

if not exist "%CORE_BUILD_DEBUG%\algorithm_management.lib" (
  echo Core library is missing: "%CORE_BUILD_DEBUG%\algorithm_management.lib"
  exit /b 1
)

if not exist "%CORE_BUILD_DEBUG%\common_data.lib" (
  echo Core library is missing: "%CORE_BUILD_DEBUG%\common_data.lib"
  exit /b 1
)

if not exist "%CORE_BUILD_DEBUG%\runtime_systems.lib" (
  echo Core library is missing: "%CORE_BUILD_DEBUG%\runtime_systems.lib"
  exit /b 1
)

pushd "%ROOT%"
"%CMAKE%" -S "%ROOT%\src\capabilities\algorithm_library" -B "%ALGO_BUILD%" --fresh -DBUILD_ALGORITHM_SAMPLE_PLUGIN=ON -DCORE_BUILD_DIR="%ROOT%\build"
if errorlevel 1 (
  set "EXITCODE=%ERRORLEVEL%"
  popd
  exit /b %EXITCODE%
)
"%CMAKE%" --build "%ALGO_BUILD%" --config Debug --target algorithm_packages -j 2
if errorlevel 1 (
  set "EXITCODE=%ERRORLEVEL%"
  popd
  exit /b %EXITCODE%
)
set "EXITCODE=0"
if not exist "%APP_LIBRARY_DIR%" mkdir "%APP_LIBRARY_DIR%"
if errorlevel 1 set "EXITCODE=1"
for /d %%D in ("%SRC_LIBRARY_DIR%\*") do (
  call :SyncAlgorithmFolder "%%~fD"
  if errorlevel 1 set "EXITCODE=1"
)
if not "%EXITCODE%"=="0" goto :cleanup
if not errorlevel 1 (
  if exist "%ROOT%\src\capabilities\algorithm_library\algorithm_catalog.json" copy /Y "%ROOT%\src\capabilities\algorithm_library\algorithm_catalog.json" "%APP_LIBRARY_DIR%\" >nul
  if exist "%ROOT%\src\capabilities\algorithm_library\agents.md" copy /Y "%ROOT%\src\capabilities\algorithm_library\agents.md" "%APP_LIBRARY_DIR%\" >nul
)
:cleanup
popd
exit /b %EXITCODE%

:SyncAlgorithmFolder
setlocal EnableExtensions
set "SOURCE_DIR=%~1"
for %%I in ("%SOURCE_DIR%") do set "ALG_NAME=%%~nxI"
set "DEST_DIR=%APP_LIBRARY_DIR%\%ALG_NAME%"
if not exist "%DEST_DIR%" mkdir "%DEST_DIR%"
if errorlevel 1 exit /b 1

for %%E in (json vert frag spv) do (
  if exist "%SOURCE_DIR%\*.%%E" (
    copy /Y "%SOURCE_DIR%\*.%%E" "%DEST_DIR%\" >nul
    if errorlevel 1 exit /b 1
  )
)

for %%C in (Debug Release) do (
  if exist "%SOURCE_DIR%\%%C\*.dll" (
    copy /Y "%SOURCE_DIR%\%%C\*.dll" "%DEST_DIR%\" >nul
    if errorlevel 1 exit /b 1
  )
  if exist "%SOURCE_DIR%\%%C\*.pdb" (
    copy /Y "%SOURCE_DIR%\%%C\*.pdb" "%DEST_DIR%\" >nul
    if errorlevel 1 exit /b 1
  )
)

endlocal
exit /b 0
