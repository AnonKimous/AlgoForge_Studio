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
set "ALGO_RELATIVE_DIR="
set "ALGO_PACKAGE_TARGET="
set "ALGO_PACKAGE_TARGETS="

for /f "usebackq tokens=1,2 delims=|" %%A in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference = 'Stop'; $sourceRoot = [System.IO.Path]::GetFullPath('%ALGO_SRC_ROOT%'); $target = '%ALGO_TARGET%'; $manifests = @(Get-ChildItem -LiteralPath $sourceRoot -Recurse -Filter ($target + '_package.json') -File); if ($manifests.Count -eq 0) { throw ('Missing algorithm package manifest: ' + $target) }; $manifest = $manifests[0]; $relativeDir = $manifest.Directory.FullName.Substring($sourceRoot.Length).TrimStart([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar); $identifier = ($relativeDir -replace '[^A-Za-z0-9_]', '_'); if ($identifier.Length -gt 0 -and $identifier[0] -match '[0-9]') { $identifier = '_' + $identifier }; Write-Output ($relativeDir + '|' + ($identifier + '_package'))"`) do (
  set "ALGO_RELATIVE_DIR=%%A"
  set "ALGO_PACKAGE_TARGET=%%B"
)

if "%ALGO_RELATIVE_DIR%"=="" (
  echo Failed to resolve algorithm package directory for "%ALGO_TARGET%".
  exit /b 1
)

if "%ALGO_PACKAGE_TARGET%"=="" (
  echo Failed to resolve algorithm package target for "%ALGO_TARGET%".
  exit /b 1
)

for /f "usebackq delims=" %%A in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference = 'Stop'; $sourceRoot = [System.IO.Path]::GetFullPath('%ALGO_SRC_ROOT%'); $packageRoot = Join-Path $sourceRoot '%ALGO_RELATIVE_DIR%'; $manifests = @(Get-ChildItem -LiteralPath $packageRoot -Recurse -Filter '*_package.json' -File | Sort-Object FullName); if ($manifests.Count -eq 0) { throw ('Missing algorithm package manifests under: ' + $packageRoot) }; $targets = foreach ($manifest in $manifests) { $relativeDir = $manifest.Directory.FullName.Substring($sourceRoot.Length).TrimStart([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar); $identifier = ($relativeDir -replace '[^A-Za-z0-9_]', '_'); if ($identifier.Length -gt 0 -and $identifier[0] -match '[0-9]') { $identifier = '_' + $identifier }; $identifier + '_package' }; Write-Output ($targets -join ' ')"`) do (
  set "ALGO_PACKAGE_TARGETS=%%A"
)

if "%ALGO_PACKAGE_TARGETS%"=="" (
  echo Failed to resolve algorithm package targets for "%ALGO_TARGET%".
  exit /b 1
)

if not exist "%ALGO_ROOT%" mkdir "%ALGO_ROOT%"
if errorlevel 1 (
  echo Failed to create algorithm root: "%ALGO_ROOT%"
  exit /b 1
)

if not exist "%ALGO_SRC_ROOT%" (
  echo Missing algorithm source root: "%ALGO_SRC_ROOT%"
  exit /b 1
)

if not exist "%ALGO_RUNTIME_ROOT%" mkdir "%ALGO_RUNTIME_ROOT%"
if errorlevel 1 (
  echo Failed to create algorithm runtime root: "%ALGO_RUNTIME_ROOT%"
  exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference = 'Stop'; Get-ChildItem -LiteralPath '%ALGO_RUNTIME_ROOT%' -Force | Remove-Item -Recurse -Force"
if errorlevel 1 (
  echo Failed to clear runtime root: "%ALGO_RUNTIME_ROOT%"
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

pushd "%ROOT%"
set "PUSHD_DONE=1"
"%CMAKE%" -S "%ALGO_ROOT%" -B "%ALGO_BUILD_DIR%" --fresh -DBUILD_ALGORITHM_SAMPLE_PLUGIN=OFF -DCORE_BUILD_DIR="%ROOT%\build" -DALGORITHM_LIBRARY_SOURCE_ROOT="%ALGO_SRC_ROOT%" -DALGORITHM_LIBRARY_RUNTIME_OUTPUT_ROOT="%ALGO_RUNTIME_ROOT%"
if errorlevel 1 (
  set "EXITCODE=1"
  goto :cleanup
)
"%CMAKE%" --build "%ALGO_BUILD_DIR%" --config Debug --target %ALGO_PACKAGE_TARGETS% --parallel
if errorlevel 1 (
  set "EXITCODE=1"
  goto :cleanup
)

powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference = 'Stop'; $root = '%ALGO_RUNTIME_ROOT%\%ALGO_RELATIVE_DIR%'; if (Test-Path -LiteralPath $root) { Get-ChildItem -LiteralPath $root -Recurse -File | Where-Object { $_.Extension -in '.exp','.lib','.pdb','.ilk','.obj','.manifest' } | Remove-Item -Force; Get-ChildItem -LiteralPath $root -Recurse -Directory | Sort-Object FullName -Descending | Where-Object { -not (Get-ChildItem -LiteralPath $_.FullName -Force) } | Remove-Item -Force }"
if errorlevel 1 (
  set "EXITCODE=1"
  goto :cleanup
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%ALGO_ROOT%\package_algorithm_runtime.ps1" -AlgorithmSourceRoot "%ALGO_SRC_ROOT%" -AlgorithmRuntimeRoot "%ALGO_RUNTIME_ROOT%"
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
