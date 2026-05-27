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

Clients connect, send a small request string, and read back a JSON response.

## Supported Requests

- `{"cmd":"snapshot"}` or plain `snapshot`
  - Returns the latest realtime frame snapshot as JSON.
- `{"cmd":"health"}` or plain `health`
  - Returns a small status JSON with the latest frame index.
- `{"cmd":"shutdown"}` or plain `shutdown`
  - Internal stop request used when the app exits.

## Snapshot Contents

The snapshot response includes:

- current frame index
- mode and phys run state
- selected vertex / triangle state
- current mesh positions and triangles
- per-vertex `delta` matrices
- active guides
- recorded frames and guide keyframes summaries
- triangle area and deformation analysis

## Notes

- The API is read-oriented for now.
- It is updated every frame in realtime.
- External tools can poll the pipe whenever they need the newest snapshot.
- Keep any platform-specific debug transport code inside this layer boundary, not in the renderer or windowing modules.
