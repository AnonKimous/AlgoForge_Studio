@echo off
setlocal EnableExtensions

set "ROOT=%~dp0.."
set "ALGORITHM_NAME=%~1"
set "TEST_DATA_ROOT=%ROOT%\testData"
set "PREVIEW_OUTPUT=%TEST_DATA_ROOT%\norm\debugInfo\render_preview.ppm"
set "RUNNER_STDOUT=%TEST_DATA_ROOT%\run_debugtool_preview_mount_stdout.txt"
set "RUNNER_STDERR=%TEST_DATA_ROOT%\run_debugtool_preview_mount_stderr.txt"
set "RUNNER_SERVER_STDOUT=%TEST_DATA_ROOT%\run_debugtool_preview_mount_server_stdout.txt"
set "RUNNER_SERVER_STDERR=%TEST_DATA_ROOT%\run_debugtool_preview_mount_server_stderr.txt"
set "RUNNER_ENDPOINT_FILE=%TEST_DATA_ROOT%\runner_control\preview_render_endpoint.txt"
set "DEBUG_TOOL=%ROOT%\build\Debug\debugTool.exe"
set "ORIG_PATH=%Path%"
set "PATH="
set "Path=%ORIG_PATH%"

if "%ALGORITHM_NAME%"=="" exit /b 1
if not exist "%DEBUG_TOOL%" exit /b 1
if not exist "%TEST_DATA_ROOT%" mkdir "%TEST_DATA_ROOT%"
if not exist "%TEST_DATA_ROOT%\norm" mkdir "%TEST_DATA_ROOT%\norm"
if not exist "%TEST_DATA_ROOT%\norm\debugInfo" mkdir "%TEST_DATA_ROOT%\norm\debugInfo"
if not exist "%TEST_DATA_ROOT%\runner_control" mkdir "%TEST_DATA_ROOT%\runner_control"
if exist "%RUNNER_STDOUT%" del /q "%RUNNER_STDOUT%"
if exist "%RUNNER_STDERR%" del /q "%RUNNER_STDERR%"
if exist "%RUNNER_SERVER_STDOUT%" del /q "%RUNNER_SERVER_STDOUT%"
if exist "%RUNNER_SERVER_STDERR%" del /q "%RUNNER_SERVER_STDERR%"
if exist "%RUNNER_ENDPOINT_FILE%" del /q "%RUNNER_ENDPOINT_FILE%"

pushd "%ROOT%"
powershell -NoProfile -ExecutionPolicy Bypass -Command "& '%DEBUG_TOOL%' --preview-render-server --algorithm '%ALGORITHM_NAME%' --ticks 12 --preview-output '%PREVIEW_OUTPUT%' --execution jobs --runner-endpoint 127.0.0.1:0 1>'%RUNNER_STDOUT%' 2>'%RUNNER_STDERR%'; exit $LASTEXITCODE"
set "EXIT_CODE=%ERRORLEVEL%"
popd
exit /b %EXIT_CODE%
