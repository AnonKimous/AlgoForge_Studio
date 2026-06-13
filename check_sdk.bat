@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
set "CMAKE=D:\Program Files\CMake\bin\cmake.exe"
set "SDK_LIB=%ROOT%\sdk\sdk.lib"
set "SDK_HEADER=%ROOT%\sdk\include\sdk_kernel.h"
set "CORE_BUILD_DEBUG=%ROOT%\build\Debug"
set "ALGO_BUILD=%ROOT%\build_algorithms"
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

if not exist "%CORE_BUILD_DEBUG%\algorithm_support.lib" (
  echo Core library is missing: "%CORE_BUILD_DEBUG%\algorithm_support.lib"
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
if not exist "%APP_LIBRARY_DIR%\temporary_test_line_motion" mkdir "%APP_LIBRARY_DIR%\temporary_test_line_motion"
if exist "%ROOT%\src\capabilities\algorithm_library\temporary_test_line_motion\*.json" copy /Y "%ROOT%\src\capabilities\algorithm_library\temporary_test_line_motion\*.json" "%APP_LIBRARY_DIR%\temporary_test_line_motion\" >nul
if exist "%ROOT%\src\capabilities\algorithm_library\temporary_test_line_motion\*.vert" copy /Y "%ROOT%\src\capabilities\algorithm_library\temporary_test_line_motion\*.vert" "%APP_LIBRARY_DIR%\temporary_test_line_motion\" >nul
if exist "%ROOT%\src\capabilities\algorithm_library\temporary_test_line_motion\*.frag" copy /Y "%ROOT%\src\capabilities\algorithm_library\temporary_test_line_motion\*.frag" "%APP_LIBRARY_DIR%\temporary_test_line_motion\" >nul
if exist "%ROOT%\src\capabilities\algorithm_library\temporary_test_line_motion\*.spv" copy /Y "%ROOT%\src\capabilities\algorithm_library\temporary_test_line_motion\*.spv" "%APP_LIBRARY_DIR%\temporary_test_line_motion\" >nul
if exist "%ROOT%\src\capabilities\algorithm_library\temporary_test_line_motion\Debug\*.dll" copy /Y "%ROOT%\src\capabilities\algorithm_library\temporary_test_line_motion\Debug\*.dll" "%APP_LIBRARY_DIR%\temporary_test_line_motion\" >nul
if exist "%ROOT%\src\capabilities\algorithm_library\temporary_test_line_motion\Release\*.dll" copy /Y "%ROOT%\src\capabilities\algorithm_library\temporary_test_line_motion\Release\*.dll" "%APP_LIBRARY_DIR%\temporary_test_line_motion\" >nul
if not exist "%APP_LIBRARY_DIR%\v6a2_triangle_collision_runtime_test" mkdir "%APP_LIBRARY_DIR%\v6a2_triangle_collision_runtime_test"
if exist "%ROOT%\src\capabilities\algorithm_library\v6a2_triangle_collision_runtime_test\*.json" copy /Y "%ROOT%\src\capabilities\algorithm_library\v6a2_triangle_collision_runtime_test\*.json" "%APP_LIBRARY_DIR%\v6a2_triangle_collision_runtime_test\" >nul
if exist "%ROOT%\src\capabilities\algorithm_library\v6a2_triangle_collision_runtime_test\*.vert" copy /Y "%ROOT%\src\capabilities\algorithm_library\v6a2_triangle_collision_runtime_test\*.vert" "%APP_LIBRARY_DIR%\v6a2_triangle_collision_runtime_test\" >nul
if exist "%ROOT%\src\capabilities\algorithm_library\v6a2_triangle_collision_runtime_test\*.frag" copy /Y "%ROOT%\src\capabilities\algorithm_library\v6a2_triangle_collision_runtime_test\*.frag" "%APP_LIBRARY_DIR%\v6a2_triangle_collision_runtime_test\" >nul
if exist "%ROOT%\src\capabilities\algorithm_library\v6a2_triangle_collision_runtime_test\Debug\*.dll" copy /Y "%ROOT%\src\capabilities\algorithm_library\v6a2_triangle_collision_runtime_test\Debug\*.dll" "%APP_LIBRARY_DIR%\v6a2_triangle_collision_runtime_test\" >nul
if exist "%ROOT%\src\capabilities\algorithm_library\v6a2_triangle_collision_runtime_test\Release\*.dll" copy /Y "%ROOT%\src\capabilities\algorithm_library\v6a2_triangle_collision_runtime_test\Release\*.dll" "%APP_LIBRARY_DIR%\v6a2_triangle_collision_runtime_test\" >nul
if exist "%ROOT%\src\capabilities\algorithm_library\v6a2_triangle_collision_runtime_test\Debug\*.pdb" copy /Y "%ROOT%\src\capabilities\algorithm_library\v6a2_triangle_collision_runtime_test\Debug\*.pdb" "%APP_LIBRARY_DIR%\v6a2_triangle_collision_runtime_test\" >nul
if exist "%ROOT%\src\capabilities\algorithm_library\v6a2_triangle_collision_runtime_test\Release\*.pdb" copy /Y "%ROOT%\src\capabilities\algorithm_library\v6a2_triangle_collision_runtime_test\Release\*.pdb" "%APP_LIBRARY_DIR%\v6a2_triangle_collision_runtime_test\" >nul
set "EXITCODE=%ERRORLEVEL%"
if not errorlevel 1 (
  if exist "%ROOT%\src\capabilities\algorithm_library\algorithm_catalog.json" copy /Y "%ROOT%\src\capabilities\algorithm_library\algorithm_catalog.json" "%APP_LIBRARY_DIR%\" >nul
  if exist "%ROOT%\src\capabilities\algorithm_library\agents.md" copy /Y "%ROOT%\src\capabilities\algorithm_library\agents.md" "%APP_LIBRARY_DIR%\" >nul
)
popd
exit /b %EXITCODE%
