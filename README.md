# min_vulkan_win32 Intervention Guide

This project exposes a realtime validation interface that lets other programs or AI agents inspect the live simulation state while the app is running.

## Platform Layout

The runtime windowing layer is organized under `src/window/` and is built on SDL3.

The validation layer is intentionally Windows-first:

- On Windows, it opens the named-pipe inspection channel documented below.
- On other platforms, the same API compiles but becomes a no-op stub.
- This keeps the main app and windowing code portable while preserving the current Windows debugging workflow.

## How To Enable The Validation Layer

Start the app with one of these command-line flags:

- `validationlayer:on`
- `vaildlayer:on`

To disable it explicitly:

- `validationlayer:off`
- `vaildlayer:off`

If the app is already running without the validation layer, stop it and relaunch it with one of the `:on` flags above.

## Validation Transport

When enabled, the app opens a Windows named pipe:

- `\\.\pipe\min_vulkan_win32_validation_layer`

This pipe is the realtime control/inspection channel for external tools.

## Supported Requests

Send a short text request or a small JSON object.

Supported commands:

- `snapshot`
- `health`
- `shutdown`

Equivalent JSON forms also work:

- `{"cmd":"snapshot"}`
- `{"cmd":"health"}`
- `{"cmd":"shutdown"}`

## What `snapshot` Returns

The `snapshot` response is JSON and describes the current frame in realtime.

It includes:

- current frame index
- interaction mode
- physics run state
- whether guides are enabled
- selected vertex / triangle state
- current mesh positions, normals, triangles, and triangle materials
- per-vertex `delta` matrices
- active physics directives
- recorded frame summaries
- guide keyframe summaries
- triangle area and deformation analysis

The schema field is:

- `validation_layer.snapshot.v1`

## Minimal PowerShell Client Example

```powershell
$pipe = New-Object System.IO.Pipes.NamedPipeClientStream(
  '.',
  'min_vulkan_win32_validation_layer',
  [System.IO.Pipes.PipeDirection]::InOut
)
$pipe.Connect(5000)

$writer = New-Object System.IO.StreamWriter($pipe)
$writer.AutoFlush = $true
$reader = New-Object System.IO.StreamReader($pipe)

$writer.Write('{"cmd":"snapshot"}')
$pipe.WaitForPipeDrain()

$response = $reader.ReadToEnd()
$response

$pipe.Dispose()
```

For a quick status check, send `{"cmd":"health"}` instead.

## Recommended Intervention Workflow For An Agent

1. Launch the app with `validationlayer:on`.
2. Connect to `\\.\pipe\min_vulkan_win32_validation_layer`.
3. Request `health` or `snapshot`.
4. Inspect the returned JSON to understand the current state.
5. If the agent needs to observe another frame, request `snapshot` again.

## Notes

- The validation layer is realtime and read-oriented.
- It is meant for inspection first, and control later.
- Do not assume the app will expose this interface unless it was launched with the validation flag.
- The app currently updates the snapshot every frame.
- If you are porting or extending this project, keep platform-specific debugging helpers behind the validation layer boundary instead of mixing them into the renderer or window modules.
