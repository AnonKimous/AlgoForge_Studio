@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
set "ENTRY=%ROOT%algorithm_studio\algorithm_studio.py"
set "PYTHON_EXE="

if not exist "%ENTRY%" (
    echo Missing entry point: "%ENTRY%"
    pause
    exit /b 1
)

if not defined PYTHON_EXE (
    if exist "D:\Program Files\anaconda\envs\pytorch\python.exe" (
        set "PYTHON_EXE=D:\Program Files\anaconda\envs\pytorch\python.exe"
    )
)

if defined VIRTUAL_ENV (
    if exist "%VIRTUAL_ENV%\Scripts\python.exe" (
        set "PYTHON_EXE=%VIRTUAL_ENV%\Scripts\python.exe"
    )
)

if not defined PYTHON_EXE (
    if defined CONDA_PREFIX (
        if exist "%CONDA_PREFIX%\python.exe" (
            set "PYTHON_EXE=%CONDA_PREFIX%\python.exe"
        )
    )
)

if not defined PYTHON_EXE (
    for /f "delims=" %%I in ('where python 2^>nul') do (
        set "PYTHON_EXE=%%I"
        goto :python_found
    )
)

if not defined PYTHON_EXE (
    for /f "delims=" %%I in ('py -3 -c "import sys; print(sys.executable)" 2^>nul') do (
        set "PYTHON_EXE=%%I"
        goto :python_found
    )
)

:python_found
if not defined PYTHON_EXE (
    echo Could not locate a usable Python interpreter.
    echo Please use the pytorch conda env, or activate your venv/conda env, or ensure python is in PATH.
    pause
    exit /b 1
)

pushd "%ROOT%"
echo Using Python: "%PYTHON_EXE%"
"%PYTHON_EXE%" -m algorithm_studio.algorithm_studio
set "EXIT_CODE=%ERRORLEVEL%"
popd

if not "%EXIT_CODE%"=="0" (
    echo algorithmDevTools exited with code %EXIT_CODE%.
    pause
)

exit /b %EXIT_CODE%
