@echo off

if "%~1"=="" (
  echo Missing algorithm build action.
  exit /b 1
)

set "ALGORITHM_BUILD_ACTION=%~1"

if /i "%ALGORITHM_BUILD_ACTION%"=="SyncLibraryRoot" goto :SyncLibraryRoot
if /i "%ALGORITHM_BUILD_ACTION%"=="SyncAlgorithmFolder" goto :SyncAlgorithmFolder
if /i "%ALGORITHM_BUILD_ACTION%"=="PruneRuntimeOutput" goto :PruneRuntimeOutput

echo Unknown algorithm build action: "%ALGORITHM_BUILD_ACTION%"
exit /b 1

:SyncLibraryRoot
set "SOURCE_ROOT=%~2"
set "DEST_ROOT=%~3"

if not exist "%SOURCE_ROOT%" (
  echo Missing algorithm source root: "%SOURCE_ROOT%"
  exit /b 1
)
if not exist "%DEST_ROOT%" mkdir "%DEST_ROOT%"
if errorlevel 1 (
  echo Failed to create destination root: "%DEST_ROOT%"
  exit /b 1
)

if exist "%SOURCE_ROOT%\algorithm_catalog.json" (
  copy /Y "%SOURCE_ROOT%\algorithm_catalog.json" "%DEST_ROOT%\" >nul
  if errorlevel 1 exit /b 1
)
if exist "%SOURCE_ROOT%\agents.md" (
  copy /Y "%SOURCE_ROOT%\agents.md" "%DEST_ROOT%\" >nul
  if errorlevel 1 exit /b 1
)
if exist "%SOURCE_ROOT%\algorithm_package_example.json" (
  copy /Y "%SOURCE_ROOT%\algorithm_package_example.json" "%DEST_ROOT%\" >nul
  if errorlevel 1 exit /b 1
)
if exist "%SOURCE_ROOT%\algorithm_plugin_api.h" (
  copy /Y "%SOURCE_ROOT%\algorithm_plugin_api.h" "%DEST_ROOT%\" >nul
  if errorlevel 1 exit /b 1
)

exit /b 0

:SyncAlgorithmFolder
set "SOURCE_DIR=%~2"
set "DEST_DIR=%~3"

if not exist "%SOURCE_DIR%" (
  echo Missing algorithm source folder: "%SOURCE_DIR%"
  exit /b 1
)

if exist "%DEST_DIR%" (
  rmdir /s /q "%DEST_DIR%"
  if errorlevel 1 exit /b 1
)
if not exist "%DEST_DIR%" mkdir "%DEST_DIR%"
if errorlevel 1 exit /b 1

for %%E in (json vert frag cpp h hpp c cc cxx inl) do (
  if exist "%SOURCE_DIR%\*.%%E" (
    copy /Y "%SOURCE_DIR%\*.%%E" "%DEST_DIR%\" >nul
    if errorlevel 1 exit /b 1
  )
)

exit /b 0

:PruneRuntimeOutput
set "RUNTIME_ROOT=%~2"

powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference = 'Stop'; $root = '%RUNTIME_ROOT%'; if (Test-Path -LiteralPath $root) { Get-ChildItem -LiteralPath $root -Recurse -File | Where-Object { $_.Extension -in '.exp','.lib','.pdb','.ilk','.obj','.manifest' } | Remove-Item -Force; Get-ChildItem -LiteralPath $root -Recurse -Directory | Sort-Object FullName -Descending | Where-Object { -not (Get-ChildItem -LiteralPath $_.FullName -Force) } | Remove-Item -Force }"
if errorlevel 1 exit /b 1

exit /b 0
