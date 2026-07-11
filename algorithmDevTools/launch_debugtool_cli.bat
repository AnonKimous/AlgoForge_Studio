@echo off
setlocal EnableExtensions

set "MODE=foreground"
if /I "%~1"=="background" set "MODE=background"
if /I "%~1"=="--background" set "MODE=background"
if /I "%~1"=="foreground" set "MODE=foreground"
if /I "%~1"=="--foreground" set "MODE=foreground"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0launch_debugtool_cli.ps1" -Mode %MODE%
exit /b %ERRORLEVEL%
