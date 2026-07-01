@echo off
setlocal EnableExtensions

set "ROOT=%~dp0.."
set "ALGO_ROOT=%ROOT%\algorithmLib"
set "ALGO_SRC_ROOT=%ALGO_ROOT%\algorithmSrc"
set "ALGO_RUNTIME_ROOT=%ALGO_ROOT%\algorithmruntimeLib"
set "TEMP_SRC_ROOT=%ALGO_ROOT%\.temp_v3a16_src"
set "DEBUG_TOOL=%ROOT%\build\Debug\debugTool.exe"
set "ORIG_PATH=%Path%"
set "PATH="
set "Path=%ORIG_PATH%"

if exist "%TEMP_SRC_ROOT%" rmdir /s /q "%TEMP_SRC_ROOT%"
mkdir "%TEMP_SRC_ROOT%"

powershell -NoProfile -ExecutionPolicy Bypass -Command "Copy-Item -LiteralPath '%ALGO_SRC_ROOT%\pipeline' -Destination '%TEMP_SRC_ROOT%' -Recurse -Force; Copy-Item -LiteralPath '%ALGO_SRC_ROOT%\algorithm_catalog.json' -Destination '%TEMP_SRC_ROOT%' -Force; Copy-Item -LiteralPath '%ALGO_SRC_ROOT%\algorithm_plugin_api.h' -Destination '%TEMP_SRC_ROOT%' -Force"
powershell -NoProfile -ExecutionPolicy Bypass -File "%ALGO_ROOT%\package_algorithm_runtime.ps1" -AlgorithmSourceRoot "%TEMP_SRC_ROOT%" -AlgorithmRuntimeRoot "%ALGO_RUNTIME_ROOT%"
set "PACKAGE_EXIT=%ERRORLEVEL%"

if exist "%TEMP_SRC_ROOT%" rmdir /s /q "%TEMP_SRC_ROOT%"
if not "%PACKAGE_EXIT%"=="0" exit /b %PACKAGE_EXIT%

pushd "%ROOT%"
set "RUNNER_ARGS=--ticks 12 --execution cpu"
if not "%~1"=="" set "RUNNER_ARGS=%*"
"%DEBUG_TOOL%" --pipeline-runner --algorithm v3a16_fireworks_pipeline_demo %RUNNER_ARGS%
set "EXIT_CODE=%ERRORLEVEL%"
popd
exit /b %EXIT_CODE%
