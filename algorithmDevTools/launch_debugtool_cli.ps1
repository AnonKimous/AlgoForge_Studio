param(
  [ValidateSet('foreground', 'background')]
  [string]$Mode = 'foreground'
)

$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$testDataRoot = Join-Path $root 'testData'
$controlDir = Join-Path $testDataRoot 'runner_control'
$endpointFile = Join-Path $controlDir 'endpoint.txt'
$serverStdout = Join-Path $controlDir 'debugtool_cli_bridge_server_stdout.txt'
$serverStderr = Join-Path $controlDir 'debugtool_cli_bridge_server_stderr.txt'
$debugTool = Join-Path $root 'build\Microsoft\RelWithDebInfo\debugTool.exe'
$buildScript = Join-Path $root 'boot\booterMSVC.py'

New-Item -ItemType Directory -Force -Path $controlDir | Out-Null
Remove-Item -Force $endpointFile, $serverStdout, $serverStderr -ErrorAction SilentlyContinue

if (-not (Test-Path -LiteralPath $debugTool)) {
  python $buildScript
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }
}

if (-not (Test-Path -LiteralPath $debugTool)) {
  exit 1
}

$server = Start-Process `
  -FilePath $debugTool `
  -ArgumentList @('--runner-server', '--runner-endpoint', '127.0.0.1:0') `
  -WorkingDirectory $root `
  -WindowStyle Hidden `
  -PassThru `
  -RedirectStandardOutput $serverStdout `
  -RedirectStandardError $serverStderr

while (-not (Test-Path -LiteralPath $endpointFile) -and -not $server.HasExited) {
  Start-Sleep -Milliseconds 100
}

if (-not (Test-Path -LiteralPath $endpointFile)) {
  if (-not $server.HasExited) {
    Stop-Process -Id $server.Id -Force
  }
  exit 1
}

$endpoint = (Get-Content -LiteralPath $endpointFile -Raw).Trim()

if ($Mode -eq 'background') {
  Write-Host "endpoint=$endpoint"
  Write-Host "pid=$($server.Id)"
  exit 0
}

$cmdLine = @(
  "set `"DEBUGTOOL_ROOT=$root`""
  "set `"DEBUGTOOL_EXE=$debugTool`""
  "set `"DEBUGTOOL_RUNNER_ENDPOINT=$endpoint`""
  "doskey dt=`"$debugTool`" --runner-endpoint $endpoint `$*"
  "echo debugTool runner endpoint: $endpoint"
  "echo use: dt --algorithm-runner --algorithm ^<name^> --ticks 1 --execution jobs"
  "cd /d `"$root`""
) -join ' && '

Start-Process -FilePath cmd.exe -ArgumentList '/k', $cmdLine -WorkingDirectory $root
