# physBased-render

This project is a layered Vulkan + SDL3 sandbox for agent-driven physics and rendering experiments.

## Runtime Model

- `debugTool` is the debug executable.
- `debug_tool_backend` is the non-UI debug host backend.
- `debug_tool_frontend` is the editor-facing debug tool surface.
- `debugTool` links the backend and frontend targets together.
- `sdk` is the external agent/algorithm submission surface and reaches algorithm assembly through `agent_management` and `agent`; it does not touch `algorithm_management` internals directly.
- An `agent` is the unit that carries algorithm packages, solver metadata, and intervention state.
- The current default workflow is: choose resource descriptors -> keep the draft prefilled -> create an agent -> run immediately.
- Algorithm packages are built by the dedicated batch tool `check_sdk.bat`; they are not compiled as part of the `debugTool` build. The checked-out package mirror lives under `algorithmLib/algorithmSrc`, and runtime DLL/SPV artifacts live under `algorithmLib/algorithmruntimeLib`.

## Terminology Rule

- If a developer writes `实例` in Chinese, treat it as `agent` unless the context explicitly says otherwise.
- This rule applies to docs, comments, UI labels, and algorithm composition notes.

## Encoding Rule

- The project uses UTF-8 for source files, comments, UI text, and build output.
- Windows builds pass `/utf-8` through the compiler so source and execution text use UTF-8 consistently.

## Current Layering

- `common_data` holds shared mesh, math, input, and interaction types.
- `runtime_systems` owns windowing, ImGui, and Vulkan runtime support.
- `common_data` also carries the shared packet structs used for algorithm support and intervention payloads.
- `algorithm_management` owns container-manifest loading, runtime container creation, plugin loading, and the unified package loader entrypoint.
- `algorithm_support` is now an internal source group under the `algorithm_management` build target; external code should include `algorithm_management/algorithm_manager.h`.
- `agent` sits above `algorithm_management` and owns the runtime agent object, including mount and submit entry points.
- Algorithm package source lives in `algorithmLib/algorithmSrc`, and built DLL/SPV runtime artifacts live in `algorithmLib/algorithmruntimeLib`.
- `capabilities/sidecar` hosts optional sidecar capabilities such as mesh import/export.
- `agent_management` owns agent creation orchestration, mount/unmount, descriptor forwarding, and ticking. It does not create containers itself.
- `debug_tool_frontend` provides the manual agent composer and live debug panel without keeping a mesh resident in app state.
- `debugTool` is the executable entry point for interactive debugging.
- `sdk` talks directly to `agent_management`, then flows into `agent`, `algorithm_management`, and `runtime_systems` through the normal layer chain. It does not depend on the debug tool frontend.
- `sdk` exposes the external agent/algorithm submission entry points.

## Execution Workflow

- The debug tool frontend and SDK create agents from resource descriptors and algorithm bindings.
- The project does not yet have a formal algorithm execution engine.
- Current algorithm execution is still a temporary bring-up path: the main thread temporarily executes algorithm work through explicitly marked `temporaryTest` hooks.
- Physics owns the evolving algorithm-side state.
- Rendering reads the current algorithm-side data to draw points and edges.
- The default preset is prefilled so the user only needs to create the agent to start work.

## SDK Boundary

- External SDK users should include `src/sdk/sdk.h`.
- The SDK should not create UI.
- The SDK should not touch reflector or intervention hooks.

## Agent Composition

- Build and submit active algorithm packages through `algorithmLib` and the normal algorithm management loader path.
- Build the final agent by attaching one or more algorithm support groups, solver config, and lightweight algorithm profiles to one agent object.
- Let upper layers assemble the agent creation spec, then let `agent_management` create and retain agents for runtime stepping while each `Agent` handles algorithm mount and submit work for its attached groups.
- The runtime does not try to validate the full graph; missing or incompatible bindings should fail at the point of use.

## Coordinate Convention

- The project uses a left-handed coordinate system.
- The origin `[0,0,0]` is at the lower-left near corner.
- `+X` points to the right.
- `+Y` points upward.
- `+Z` points into the screen.
- Vulkan viewport setup in the runtime flips framebuffer coordinates to match this convention.
- Render Preview shaders interpret `x` and `y` as preview-page pixel coordinates.
- In Render Preview, `[0,0]` is the lower-left corner of the ImGui content region below the title bar.

## Logs

- `DEVlog/2026-06-01_devlog.md` records the second June 1 modification pass.
- Older architecture notes live under `DEVlog/`.

