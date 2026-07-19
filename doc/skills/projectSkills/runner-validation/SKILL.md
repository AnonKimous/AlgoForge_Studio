---
name: runner-validation
description: Validate completed algorithm, pipeline, runtime, or preview changes with the repository debugTool runner. Use after implementation or rebuilding an algorithm, especially when execution, staging, timing, or preview output could be affected.
---

# Runner Validation

Use the repository runner as the completion check for changes that affect algorithms, pipeline scheduling, Vulkan execution, preview rendering, or debugTool behavior.

## Required workflow

1. Build the changed target with the repository-provided build script.
2. Start a runner server and keep it alive while the client runs. From the repository root:

   ```powershell
   cmd /c start "" /b build\Microsoft\RelWithDebInfo\debugTool.exe --runner-server --runner-server-once --runner-endpoint 127.0.0.1:0
   Start-Sleep -Seconds 2
   cmd /c build\Microsoft\RelWithDebInfo\debugTool.exe --pipeline-runner --algorithm <algorithm> --ticks 1 --execution jobs --preview-output testData\runner_preview.png
   ```

   Use `--algorithm-runner` for a non-pipeline algorithm. The client discovers the server endpoint from `testData\runner_control\endpoint.txt`.

3. Require the client response to contain `OK pipeline_runner` or `OK algorithm_runner`.
4. Inspect `testData\pipeline\debugInfo\last_run.log` or `testData\norm\debugInfo\last_run.log`.
5. Inspect the newest CSV under `testData\pipeline_timing` when a pipeline is involved. Record each stage duration and look for stalled stages, timeout messages, or unusually large elapsed values.
6. Confirm that the requested preview output exists under `testData\` and that the log reports nonzero preview pixels when the algorithm declares a render result.

## Common commands

Run these from the repository root. Keep every output path under `testData\`.

```powershell
# Build the debugTool used by the runner.
python boot\booterMSVC.py

# Build a changed algorithm package with debug information.
python boot\booterNinjaClang.py <algorithm>

# Run one ordinary algorithm tick.
cmd /c start "" /b build\Microsoft\RelWithDebInfo\debugTool.exe --runner-server --runner-server-once --runner-endpoint 127.0.0.1:0
Start-Sleep -Seconds 2
cmd /c build\Microsoft\RelWithDebInfo\debugTool.exe --algorithm-runner --algorithm <algorithm> --ticks 1 --execution jobs --preview-output testData\algorithm_preview.png

# Run one pipeline tick.
cmd /c start "" /b build\Microsoft\RelWithDebInfo\debugTool.exe --runner-server --runner-server-once --runner-endpoint 127.0.0.1:0
Start-Sleep -Seconds 2
cmd /c build\Microsoft\RelWithDebInfo\debugTool.exe --pipeline-runner --algorithm <pipeline> --ticks 1 --execution jobs --preview-output testData\pipeline_preview.png

# Exercise the VK path for repeated ticks.
cmd /c start "" /b build\Microsoft\RelWithDebInfo\debugTool.exe --runner-server --runner-server-once --runner-endpoint 127.0.0.1:0
Start-Sleep -Seconds 2
cmd /c build\Microsoft\RelWithDebInfo\debugTool.exe --pipeline-runner --algorithm <pipeline> --ticks 12 --execution vk --preview-output testData\pipeline_vk_preview.png
```

Do not run only the client command. If the output stops at `runner_client.begin`, the server was not connected and the test did not run.

For every completed change, run at least one matching runner command after the final successful build. For preview changes, run both an ordinary algorithm and a pipeline when both paths are in scope.

## Failure interpretation

- A client message containing only `runner_client.begin` means no server accepted the request. It is not a successful validation.
- `OK pipeline_runner` proves execution completed, but does not prove preview rendering worked. Check `preview_pixels`, `pipeline`, and `target` in `pipeline_runner.end`.
- `pipeline=not_ready`, `target=not_ready`, or `preview_pixels=0` is a validation failure for a drawable preview.
- A timing CSV with stage rows but no stalled reason proves the scheduler completed the measured tick; it does not hide a preview setup failure.

Keep all runner logs, images, and temporary outputs under `testData\`. Do not write transient runner output at the repository root.
