---
name: debugtool-cli-runner
description: Use the debugTool command-line frontend and runner-server protocol to execute algorithms or pipelines, export previews, inspect timing, and validate jobs, Vulkan, or CUDA behavior. Use when connecting to the CLI, adding runner commands, or testing debugTool behavior.
---

# DebugTool CLI Runner

The CLI frontend is implemented in `src/debug_tool/main.cpp`. It talks to the same debugTool backend runtime used by the GUI, but the CLI is a separate process frontend. The runner protocol uses a server process and a client process.

## Server and client connection

Start the server from the repository root:

```powershell
cmd /c start "" /b build\Debug\debugTool.exe --runner-server --runner-server-once --runner-endpoint 127.0.0.1:0
Start-Sleep -Seconds 2
```

The server writes the selected endpoint to `testData\runner_control\endpoint.txt`. A client using `--runner-endpoint 127.0.0.1:0` resolves that file. Existing PowerShell probes under `testData` use `ProcessStartInfo` and wait for the endpoint before starting the client; prefer those probes when a reliable one-shot test is needed.

Do not treat output that stops at `runner_client.begin` as a test result. The server must log the request and a response, and the client must receive `OK algorithm_runner` or `OK pipeline_runner`.

## Current commands

`debugTool.exe --algorithm-runner` runs one ordinary algorithm. `debugTool.exe --pipeline-runner` mounts and ticks a pipeline. Both accept:

- `--algorithm <name>`
- `--ticks <positive integer>`
- `--preview-width <positive integer>`
- `--preview-height <positive integer>`
- `--preview-output <path>`
- `--execution jobs|vk|cuda`
- `--runner-endpoint <host:port>`

Pipeline runner additionally accepts `--pipeline-name <name>`.

The other CLI modes are:

- `--runner-server` and `--runner-server-once` for the control server;
- `--preview-render-server` for the preview render control endpoint;
- `--help` or `-h` for the authoritative command summary.

## Validation commands

Run from the repository root and keep outputs under `testData`:

```powershell
python boot\booterMSVC.py
cmd /c start "" /b build\Debug\debugTool.exe --runner-server --runner-server-once --runner-endpoint 127.0.0.1:0
Start-Sleep -Seconds 2
cmd /c build\Debug\debugTool.exe --algorithm-runner --algorithm <name> --ticks 1 --execution jobs --preview-output testData\algorithm_preview.ppm
```

For a pipeline:

```powershell
cmd /c start "" /b build\Debug\debugTool.exe --runner-server --runner-server-once --runner-endpoint 127.0.0.1:0
Start-Sleep -Seconds 2
cmd /c build\Debug\debugTool.exe --pipeline-runner --algorithm <pipeline> --ticks 1 --execution vk --preview-output testData\pipeline_vk_preview.ppm
```

Inspect `testData\pipeline\debugInfo\last_run.log` or `testData\norm\debugInfo\last_run.log`. For preview work require the exported file, nonzero `preview_pixels`, and a ready preview pipeline/target. Inspect the newest CSV in `testData\pipeline_timing` for stage timings. `OK` alone proves execution returned, not that a preview was visible.

## CLI changes

When adding a command, update the option parser and help text in `main.cpp`, then update the runner request handling if the command must cross the server boundary. Keep GUI and CLI behavior in the debugTool frontend/backend boundary; do not expose debug-only controls through the SDK unless explicitly requested.

## Python facade

The repository also provides the `aglopy` package at `aglopy\`. It wraps the same runner-server protocol for Python scripts:

```python
from aglopy import DebugToolRunner

with DebugToolRunner(repo_root) as runner:
    result = runner.run_pipeline(
        "v4a16_fireworks_pipeline_demo",
        execution="vk",
        preview_output=repo_root / "testData" / "python_preview.ppm",
    ).require_ok()
```

Use `aglopy\README.md` for the public API. The package uses the configured `build\Debug\debugTool.exe`, keeps the server alive for multiple requests, and returns the runner response, log path, preview path, and parsed preview pixel count.
