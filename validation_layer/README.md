# Validation Layer

This folder documents the realtime validation API exposed by the app.

## Platform Policy

- Windows is the primary debugging target.
- The realtime pipe implementation lives in the Windows build.
- On other platforms, the same API compiles as a no-op stub so the main app stays portable.

## Enable

Start the program with one of these flags:

- `validationlayer:on`
- `vaildlayer:on`

Use `validationlayer:off` or `vaildlayer:off` to disable it explicitly.

## Transport

The app opens a Windows named pipe:

`\\.\pipe\min_vulkan_win32_validation_layer`

Clients connect, send a small request string or script, and read back a JSON response.

## Supported Requests

- `{"cmd":"snapshot"}` or plain `snapshot`
  - Returns the latest realtime frame snapshot as JSON.
- `{"cmd":"health"}` or plain `health`
  - Returns a small status JSON with the latest frame index.
- `{"cmd":"physstep"}` or plain `physstep`
  - Queues one physics step action.
- `{"cmd":"physstep 5"}` or plain `physstep 5`
  - Queues one physics step action with `step_count=5`.
- `{"cmd":"script","script":"physstep 5; pause; reset"}` or plain `script physstep 5; pause; reset`
  - Parses a small script into abstract validation actions.
- `{"cmd":"shutdown"}` or plain `shutdown`
  - Internal stop request used when the app exits.

## Script Commands

The script system is intentionally abstract. It does not call renderer or physics methods directly.

Supported commands:

- `physstep N`
- `reset`
- `run`
- `pause`
- `guide on`
- `guide off`

The app consumes the queued actions through a glue layer that maps them to real runtime calls.

## Snapshot Contents

The snapshot response includes:

- current frame index
- mode and phys run state
- selected vertex / triangle state
- current mesh positions and triangles
- per-vertex `delta` matrices
- active displacement guides
- active velocity guides
- active force guides
- recorded frames and guide keyframes summaries
- triangle area and deformation analysis

## Physics States

- `Run`
- `Pause`

Reset is an action that returns the simulation to its initial state instead of a persistent `Stop` mode.

Guide semantics:

- `guide displacement` is legality-checked against the mesh state.
- `guide velocity` directly overwrites the selected vertices' velocity matrices.
- `guide force` is scheduled by frame offset and duration, and is applied in future frames through the physics layer.
- Holding `Ctrl` while selecting vertices in the UI lets multiple vertices share one guide group.

## Notes

- The API is read-oriented for now.
- It is updated every frame in realtime.
- External tools can poll the pipe whenever they need the newest snapshot.
- Keep any platform-specific debug transport code inside this layer boundary, not in the renderer or windowing modules.
