@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
set "CMAKE=cmake"
set "ALGO_ROOT=%ROOT%\algorithmLib"
set "ALGO_SRC_ROOT=%ALGO_ROOT%\algorithmSrc"
set "ALGO_RUNTIME_ROOT=%ALGO_ROOT%\algorithmruntimeLib"
set "ALGO_BUILD_DIR=%ALGO_ROOT%\.build"
set "ORIG_PATH=%Path%"
set "PATH="
set "Path=%ORIG_PATH%"
set "PUSHD_DONE=0"

if "%~1"=="" (
  echo Missing algorithm target name.
  exit /b 1
)

set "ALGO_TARGET=%~1"

if not exist "%ALGO_ROOT%" mkdir "%ALGO_ROOT%"
if errorlevel 1 (
  echo Failed to create algorithm root: "%ALGO_ROOT%"
  exit /b 1
)

if not exist "%ALGO_SRC_ROOT%" (
  echo Missing algorithm source root: "%ALGO_SRC_ROOT%"
  exit /b 1
)

if exist "%ALGO_RUNTIME_ROOT%" (
  rmdir /s /q "%ALGO_RUNTIME_ROOT%"
  if errorlevel 1 (
    echo Failed to reset algorithm runtime root: "%ALGO_RUNTIME_ROOT%"
    exit /b 1
  )
)
if not exist "%ALGO_RUNTIME_ROOT%" mkdir "%ALGO_RUNTIME_ROOT%"
if errorlevel 1 (
  echo Failed to create algorithm runtime root: "%ALGO_RUNTIME_ROOT%"
  exit /b 1
)

if exist "%ALGO_BUILD_DIR%" (
  rmdir /s /q "%ALGO_BUILD_DIR%"
  if errorlevel 1 (
    echo Failed to reset temporary build root: "%ALGO_BUILD_DIR%"
    exit /b 1
  )
)
if not exist "%ALGO_BUILD_DIR%" mkdir "%ALGO_BUILD_DIR%"
if errorlevel 1 (
  echo Failed to create temporary build root: "%ALGO_BUILD_DIR%"
  exit /b 1
)

if exist "%ALGO_RUNTIME_ROOT%\%ALGO_TARGET%" (
  rmdir /s /q "%ALGO_RUNTIME_ROOT%\%ALGO_TARGET%"
  if errorlevel 1 (
    echo Failed to reset runtime output folder: "%ALGO_RUNTIME_ROOT%\%ALGO_TARGET%"
    set "EXITCODE=1"
    goto :cleanup
  )
)

pushd "%ROOT%"
set "PUSHD_DONE=1"
"%CMAKE%" -S "%ALGO_ROOT%" -B "%ALGO_BUILD_DIR%" --fresh -DBUILD_ALGORITHM_SAMPLE_PLUGIN=ON -DCORE_BUILD_DIR="%ROOT%\build" -DALGORITHM_LIBRARY_SOURCE_ROOT="%ALGO_SRC_ROOT%" -DALGORITHM_LIBRARY_RUNTIME_OUTPUT_ROOT="%ALGO_RUNTIME_ROOT%"
if errorlevel 1 (
  set "EXITCODE=1"
  goto :cleanup
)
"%CMAKE%" --build "%ALGO_BUILD_DIR%" --config Debug --target "%ALGO_TARGET%_package" --parallel
if errorlevel 1 (
  set "EXITCODE=1"
  goto :cleanup
)

powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference = 'Stop'; $root = '%ALGO_RUNTIME_ROOT%\%ALGO_TARGET%'; if (Test-Path -LiteralPath $root) { Get-ChildItem -LiteralPath $root -Recurse -File | Where-Object { $_.Extension -in '.exp','.lib','.pdb','.ilk','.obj','.manifest' } | Remove-Item -Force; Get-ChildItem -LiteralPath $root -Recurse -Directory | Sort-Object FullName -Descending | Where-Object { -not (Get-ChildItem -LiteralPath $_.FullName -Force) } | Remove-Item -Force }"
if errorlevel 1 (
  set "EXITCODE=1"
  goto :cleanup
)

set "EXITCODE=0"

:cleanup
if "%PUSHD_DONE%"=="1" popd
if exist "%ALGO_BUILD_DIR%" (
  rmdir /s /q "%ALGO_BUILD_DIR%"
)
exit /b %EXITCODE%
