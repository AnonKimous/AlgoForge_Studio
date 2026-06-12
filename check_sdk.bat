@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
set "CMAKE=D:\Program Files\CMake\bin\cmake.exe"
set "VCVARS64=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
set "GLSLC=D:\VulkanSDK\1.4.341.1\Bin\glslc.exe"
set "SDK_LIB=%ROOT%\sdk\sdk.lib"
set "SDK_HEADER=%ROOT%\sdk\include\sdk_kernel.h"
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

pushd "%ROOT%"
"%CMAKE%" -S "%ROOT%" -B "%ALGO_BUILD%" --fresh -DBUILD_DEBUG_TOOL_SAMPLE_PLUGIN=ON
if errorlevel 1 (
  set "EXITCODE=%ERRORLEVEL%"
  popd
  exit /b %EXITCODE%
)
"%CMAKE%" --build "%ALGO_BUILD%" --config Debug --target temporary_test_line_motion_plugin -j 2
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
call "%VCVARS64%" >nul
if errorlevel 1 (
  set "EXITCODE=%ERRORLEVEL%"
  popd
  exit /b %EXITCODE%
)
"%GLSLC%" -o "%APP_LIBRARY_DIR%\v6a2_triangle_collision_runtime_test\v6a2_triangle_collision_runtime_test_result_render.vert.spv" "%APP_LIBRARY_DIR%\v6a2_triangle_collision_runtime_test\v6a2_triangle_collision_runtime_test_result_render.vert"
if errorlevel 1 (
  set "EXITCODE=%ERRORLEVEL%"
  popd
  exit /b %EXITCODE%
)
"%GLSLC%" -o "%APP_LIBRARY_DIR%\v6a2_triangle_collision_runtime_test\v6a2_triangle_collision_runtime_test_result_render.frag.spv" "%APP_LIBRARY_DIR%\v6a2_triangle_collision_runtime_test\v6a2_triangle_collision_runtime_test_result_render.frag"
if errorlevel 1 (
  set "EXITCODE=%ERRORLEVEL%"
  popd
  exit /b %EXITCODE%
)
cl /nologo /std:c++20 /EHsc /MDd /Zi /D _DEBUG /D ALGORITHM_LIBRARY_PLUGIN_BUILD=1 /D ALGORITHM_LIBRARY_RESOURCE_ROOT="%ROOT%\src\capabilities\algorithm_library" ^
  /I"%ROOT%\src" /I"%ROOT%\third_party\cjson" /I"%ROOT%\third_party\SDL-main\include" /I"%ROOT%\third_party\eigen" ^
  /I"%ROOT%\third_party\VulkanMemoryAllocator-3.3.0\include" /I"%ROOT%\third_party\SPIRV-Reflect-vulkan-sdk-1.4.350.0" ^
  /I"%ROOT%\third_party\vkb\src" /I"%ROOT%\third_party\imgui-1.92.8-docking" /I"%ROOT%\third_party\imgui-1.92.8-docking\backends" ^
  /Fo"%ALGO_BUILD%\v6a2_triangle_collision_runtime_test_plugin.obj" /c "%ROOT%\src\capabilities\algorithm_library\v6a2_triangle_collision_runtime_test\v6a2_triangle_collision_runtime_test_plugin.cpp"
if errorlevel 1 (
  set "EXITCODE=%ERRORLEVEL%"
  popd
  exit /b %EXITCODE%
)
link /nologo /DLL /OUT:"%APP_LIBRARY_DIR%\v6a2_triangle_collision_runtime_test\v6a2_triangle_collision_runtime_test.dll" ^
  /IMPLIB:"%APP_LIBRARY_DIR%\v6a2_triangle_collision_runtime_test\v6a2_triangle_collision_runtime_test.lib" ^
  /PDB:"%APP_LIBRARY_DIR%\v6a2_triangle_collision_runtime_test\v6a2_triangle_collision_runtime_test.pdb" ^
  "%ALGO_BUILD%\v6a2_triangle_collision_runtime_test_plugin.obj" /LIBPATH:"%ALGO_BUILD%\Debug" /LIBPATH:"D:\VulkanSDK\1.4.341.1\Lib" algorithm_support.lib algorithm_management.lib common_data.lib runtime_systems.lib vulkan-1.lib SDL3.lib
if errorlevel 1 (
  set "EXITCODE=%ERRORLEVEL%"
  popd
  exit /b %EXITCODE%
)
set "EXITCODE=%ERRORLEVEL%"
if not errorlevel 1 (
  if exist "%ROOT%\src\capabilities\algorithm_library\algorithm_catalog.json" copy /Y "%ROOT%\src\capabilities\algorithm_library\algorithm_catalog.json" "%APP_LIBRARY_DIR%\" >nul
  if exist "%ROOT%\src\capabilities\algorithm_library\agents.md" copy /Y "%ROOT%\src\capabilities\algorithm_library\agents.md" "%APP_LIBRARY_DIR%\" >nul
)
popd
exit /b %EXITCODE%
