@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT=%~dp0"
set "APP_DIR=%ROOT%algorithm_studio"
set "ENTRY=%APP_DIR%\algorithm_studio.py"
set "REQUIREMENTS=%APP_DIR%\requirements.txt"
set "CONDA_BAT="
set "CONDA_ROOT="
set "CONDA_DIR="
set "PYTHON_EXE="

for /f "delims=" %%I in ('where conda.bat 2^>nul') do (
    set "CONDA_BAT=%%I"
    goto :found_conda
)

:found_conda
if defined CONDA_BAT (
    for %%I in ("!CONDA_BAT!") do set "CONDA_DIR=%%~dpI"
    echo !CONDA_DIR! | findstr /i "\\condabin\\" >nul
    if not errorlevel 1 (
        for %%I in ("!CONDA_DIR!..") do set "CONDA_ROOT=%%~fI"
    ) else (
        echo !CONDA_DIR! | findstr /i "\\Library\\bin\\" >nul
        if not errorlevel 1 (
            for %%I in ("!CONDA_DIR!..\..") do set "CONDA_ROOT=%%~fI"
        ) else (
            echo Unrecognized conda.bat location: !CONDA_BAT!
            exit /b 1
        )
    )
)

set "PYTHON_EXE=!CONDA_ROOT!\envs\pytorch\python.exe"

if exist "%REQUIREMENTS%" (
    for %%F in ("%REQUIREMENTS%") do if %%~zF gtr 0 "!PYTHON_EXE!" -m pip install -r "!REQUIREMENTS!"
)

start "" "!PYTHON_EXE!" "!ENTRY!"
exit /b 0

echo conda.bat was not found.
exit /b 1
