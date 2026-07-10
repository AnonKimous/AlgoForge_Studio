@echo off
setlocal EnableExtensions

set "ROOT=%~dp0.."
set "ALGO_ROOT=%ROOT%\algorithmLib"
set "ALGO_SRC_ROOT=%ALGO_ROOT%\algorithmSrc"
set "ALGO_RUNTIME_ROOT=%ALGO_ROOT%\algorithmruntimeLib"
set "TEMP_SRC_ROOT=%ALGO_ROOT%\.temp_v3a16_src"
set "TEST_DATA_ROOT=%ROOT%\testData"
set "RUNNER_STDOUT=%TEST_DATA_ROOT%\run_debugtool_runner_stdout.txt"
set "RUNNER_STDERR=%TEST_DATA_ROOT%\run_debugtool_runner_stderr.txt"
set "RUNNER_SERVER_STDOUT=%TEST_DATA_ROOT%\run_debugtool_runner_server_stdout.txt"
set "RUNNER_SERVER_STDERR=%TEST_DATA_ROOT%\run_debugtool_runner_server_stderr.txt"
set "RUNNER_ENDPOINT_FILE=%TEST_DATA_ROOT%\runner_control\endpoint.txt"
set "DEBUG_TOOL=%ROOT%\build\Debug\debugTool.exe"
set "ORIG_PATH=%Path%"
set "PATH="
set "Path=%ORIG_PATH%"

if exist "%TEMP_SRC_ROOT%" rmdir /s /q "%TEMP_SRC_ROOT%"
mkdir "%TEMP_SRC_ROOT%"
if not exist "%TEST_DATA_ROOT%" mkdir "%TEST_DATA_ROOT%"
if exist "%RUNNER_STDOUT%" del /q "%RUNNER_STDOUT%"
if exist "%RUNNER_STDERR%" del /q "%RUNNER_STDERR%"
if exist "%RUNNER_SERVER_STDOUT%" del /q "%RUNNER_SERVER_STDOUT%"
if exist "%RUNNER_SERVER_STDERR%" del /q "%RUNNER_SERVER_STDERR%"
if exist "%RUNNER_ENDPOINT_FILE%" del /q "%RUNNER_ENDPOINT_FILE%"
if exist "%TEST_DATA_ROOT%\runner_control\server.log" del /q "%TEST_DATA_ROOT%\runner_control\server.log"
if exist "%TEST_DATA_ROOT%\runner_control\client.log" del /q "%TEST_DATA_ROOT%\runner_control\client.log"

powershell -NoProfile -ExecutionPolicy Bypass -Command "Copy-Item -LiteralPath '%ALGO_SRC_ROOT%\pipeline' -Destination '%TEMP_SRC_ROOT%' -Recurse -Force; Copy-Item -LiteralPath '%ALGO_SRC_ROOT%\algorithm_plugin_api.h' -Destination '%TEMP_SRC_ROOT%' -Force"
powershell -NoProfile -ExecutionPolicy Bypass -File "%ALGO_ROOT%\cache_algorithm_runtime.ps1" -AlgorithmSourceRoot "%TEMP_SRC_ROOT%" -AlgorithmRuntimeRoot "%ALGO_RUNTIME_ROOT%"
set "PACKAGE_EXIT=%ERRORLEVEL%"

if exist "%TEMP_SRC_ROOT%" rmdir /s /q "%TEMP_SRC_ROOT%"
if not exist "%ALGO_RUNTIME_ROOT%\pipeline\v3a16_fireworks_pipeline_demo\Debug\v3a16_fireworks_pipeline_demo.dll" exit /b %PACKAGE_EXIT%

pushd "%ROOT%"
set "RUNNER_ARGS=--ticks 12 --execution jobs"
if not "%~1"=="" set "RUNNER_ARGS=%*"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$endpointFile = '%RUNNER_ENDPOINT_FILE%'; $serverArgs = '--runner-server --runner-endpoint 127.0.0.1:0 --runner-server-once'; $psi = [System.Diagnostics.ProcessStartInfo]::new(); $psi.FileName = '%DEBUG_TOOL%'; $psi.Arguments = $serverArgs; $psi.WorkingDirectory = '%ROOT%'; $psi.UseShellExecute = $false; $psi.CreateNoWindow = $true; $server = [System.Diagnostics.Process]::Start($psi); while (-not (Test-Path $endpointFile)) { Start-Sleep -Milliseconds 100 }; & '%DEBUG_TOOL%' --pipeline-runner --algorithm v3a16_fireworks_pipeline_demo %RUNNER_ARGS% --runner-endpoint 127.0.0.1:0 1>'%RUNNER_STDOUT%' 2>'%RUNNER_STDERR%'; $server.WaitForExit()"
set "EXIT_CODE=%ERRORLEVEL%"
popd
exit /b %EXIT_CODE%
