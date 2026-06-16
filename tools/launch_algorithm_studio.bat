@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
set "ENTRY=%ROOT%algorithm_studio\algorithm_studio.py"

if not exist "%ENTRY%" (
    echo Missing entry point: "%ENTRY%"
    pause
    exit /b 1
)

where py >nul 2>nul
if errorlevel 1 (
    echo Python launcher "py" was not found.
    pause
    exit /b 1
)

pushd "%ROOT%"
py -3.9 -m algorithm_studio.algorithm_studio
set "EXIT_CODE=%ERRORLEVEL%"
popd

if not "%EXIT_CODE%"=="0" (
    echo Algorithm Studio exited with code %EXIT_CODE%.
    pause
)

exit /b %EXIT_CODE%
