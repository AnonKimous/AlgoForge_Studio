@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
set "CMAKE=cmake"
set "ALGO_ROOT=%ROOT%\algorithmLib"
set "ALGO_SRC_ROOT=%ALGO_ROOT%\algorithmSrc"
set "ALGO_RUNTIME_ROOT=%ALGO_ROOT%\algorithmruntimeLib\releaseWithDebugInfo"
set "ALGO_BUILD_DIR=%ALGO_ROOT%\.build_relwithdebinfo"
set "ALGO_BUILD_CONFIGURATION=RelWithDebInfo"
set "LLVM_ROOT=%ROOT%\.toolchains\llvm-22.1.8"
set "CLANG_CL=%LLVM_ROOT%\bin\clang-cl.exe"
set "ORIG_PATH=%Path%"
set "PATH="
set "Path=%ORIG_PATH%"
set "CL=/D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH %CL%"
set "PUSHD_DONE=0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VS_INSTALLATION_PATH="
set "VS_VSDEVCMD="
set "NINJA_EXE="
set "CC=%CLANG_CL%"
set "CXX=%CLANG_CL%"

if not exist "%VSWHERE%" (
  echo Missing Visual Studio installer helper: "%VSWHERE%"
  exit /b 1
)

for /f "usebackq delims=" %%A in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_INSTALLATION_PATH=%%A"

if "%VS_INSTALLATION_PATH%"=="" (
  echo Failed to locate a Visual Studio installation with C++ tools.
  exit /b 1
)

set "VS_VSDEVCMD=%VS_INSTALLATION_PATH%\Common7\Tools\VsDevCmd.bat"
set "NINJA_EXE=%VS_INSTALLATION_PATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"

if not exist "%VS_VSDEVCMD%" (
  echo Missing Visual Studio developer command script: "%VS_VSDEVCMD%"
  exit /b 1
)

if not exist "%NINJA_EXE%" (
  echo Missing Ninja executable: "%NINJA_EXE%"
  exit /b 1
)

if not exist "%CLANG_CL%" (
  echo Missing LLVM clang-cl executable: "%CLANG_CL%"
  exit /b 1
)

call "%VS_VSDEVCMD%" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 (
  echo Failed to initialize Visual Studio build environment.
  exit /b 1
)

if "%~1"=="" (
  echo Missing algorithm target name.
  exit /b 1
)

set "ALGO_TARGET=%~1"
set "ALGO_RELATIVE_DIR="
set "ALGO_ALGO_TARGET="
set "ALGO_ALGO_TARGETS="

for /f "usebackq tokens=1,2 delims=|" %%A in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference = 'Stop'; $sourceRoot = [System.IO.Path]::GetFullPath('%ALGO_SRC_ROOT%'); $target = '%ALGO_TARGET%'; $manifests = @(Get-ChildItem -LiteralPath $sourceRoot -Recurse -Filter 'manifest.json' -File | Where-Object { $_.Directory.Name -eq $target }); if ($manifests.Count -eq 0) { throw ('Missing algorithm manifest: ' + $target) }; $manifest = $manifests[0]; $relativeDir = $manifest.Directory.FullName.Substring($sourceRoot.Length).TrimStart([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar); $identifier = ($relativeDir -replace '[^A-Za-z0-9_]', '_'); if ($identifier.Length -gt 0 -and $identifier[0] -match '[0-9]') { $identifier = '_' + $identifier }; Write-Output ($relativeDir + '|' + ($identifier + '_algo'))"`) do (
  set "ALGO_RELATIVE_DIR=%%A"
  set "ALGO_ALGO_TARGET=%%B"
)

if "%ALGO_RELATIVE_DIR%"=="" (
  echo Failed to resolve algorithm algo directory for "%ALGO_TARGET%".
  exit /b 1
)

if "%ALGO_ALGO_TARGET%"=="" (
  echo Failed to resolve algorithm algo target for "%ALGO_TARGET%".
  exit /b 1
)

for /f "usebackq delims=" %%A in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference = 'Stop'; $sourceRoot = [System.IO.Path]::GetFullPath('%ALGO_SRC_ROOT%'); $algoRoot = Join-Path $sourceRoot '%ALGO_RELATIVE_DIR%'; $manifests = @(Get-ChildItem -LiteralPath $algoRoot -Recurse -Filter 'manifest.json' -File | Sort-Object FullName); if ($manifests.Count -eq 0) { throw ('Missing algorithm manifests under: ' + $algoRoot) }; $targets = foreach ($manifest in $manifests) { $relativeDir = $manifest.Directory.FullName.Substring($sourceRoot.Length).TrimStart([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar); $identifier = ($relativeDir -replace '[^A-Za-z0-9_]', '_'); if ($identifier.Length -gt 0 -and $identifier[0] -match '[0-9]') { $identifier = '_' + $identifier }; $identifier + '_algo' }; Write-Output ($targets -join ' ')"`) do (
  set "ALGO_ALGO_TARGETS=%%A"
)

if "%ALGO_ALGO_TARGETS%"=="" (
  echo Failed to resolve algorithm algo targets for "%ALGO_TARGET%".
  exit /b 1
)

if not exist "%ALGO_ROOT%" mkdir "%ALGO_ROOT%"
if not exist "%ALGO_ROOT%" (
  echo Failed to create algorithm root: "%ALGO_ROOT%"
  exit /b 1
)

if not exist "%ALGO_SRC_ROOT%" (
  echo Missing algorithm source root: "%ALGO_SRC_ROOT%"
  exit /b 1
)

if not exist "%ALGO_RUNTIME_ROOT%" mkdir "%ALGO_RUNTIME_ROOT%"
if not exist "%ALGO_RUNTIME_ROOT%" (
  echo Failed to create algorithm runtime root: "%ALGO_RUNTIME_ROOT%"
  exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference = 'Stop'; $targetRuntimeDir = Join-Path '%ALGO_RUNTIME_ROOT%' '%ALGO_RELATIVE_DIR%'; if (Test-Path -LiteralPath $targetRuntimeDir) { Remove-Item -LiteralPath $targetRuntimeDir -Recurse -Force }"
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
if not exist "%ALGO_BUILD_DIR%" (
  echo Failed to create temporary build root: "%ALGO_BUILD_DIR%"
  exit /b 1
)

pushd "%ROOT%"
set "PUSHD_DONE=1"
"%CMAKE%" -S "%ALGO_ROOT%" -B "%ALGO_BUILD_DIR%" --fresh -G Ninja -DCMAKE_BUILD_TYPE=%ALGO_BUILD_CONFIGURATION% -DCMAKE_MAKE_PROGRAM="%NINJA_EXE%" -DCMAKE_C_COMPILER="%CC%" -DCMAKE_CXX_COMPILER="%CXX%" -DBUILD_ALGORITHM_SAMPLE_PLUGIN=OFF -DCORE_BUILD_DIR="%ROOT%\build" -DALGORITHM_LIBRARY_SOURCE_ROOT="%ALGO_SRC_ROOT%" -DALGORITHM_LIBRARY_RUNTIME_OUTPUT_ROOT="%ALGO_RUNTIME_ROOT%"
if errorlevel 1 (
  set "EXITCODE=1"
  goto :cleanup
)
"%CMAKE%" --build "%ALGO_BUILD_DIR%" --target %ALGO_ALGO_TARGETS% --parallel
if errorlevel 1 (
  set "EXITCODE=1"
  goto :cleanup
)

powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference = 'Stop'; $root = '%ALGO_RUNTIME_ROOT%\%ALGO_RELATIVE_DIR%'; if (Test-Path -LiteralPath $root) { Get-ChildItem -LiteralPath $root -Recurse -File | Where-Object { $_.Extension -in '.exp','.lib','.pdb','.ilk','.obj','.manifest' } | Remove-Item -Force; Get-ChildItem -LiteralPath $root -Recurse -Directory | Sort-Object FullName -Descending | Where-Object { -not (Get-ChildItem -LiteralPath $_.FullName -Force) } | Remove-Item -Force }"
if errorlevel 1 (
  set "EXITCODE=1"
  goto :cleanup
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%ALGO_ROOT%\cache_algorithm_runtime.ps1" -AlgorithmSourceRoot "%ALGO_SRC_ROOT%" -AlgorithmRuntimeRoot "%ALGO_RUNTIME_ROOT%" -AlgorithmBuildConfiguration "%ALGO_BUILD_CONFIGURATION%"
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
