# physBased-render

This project is a layered Vulkan + SDL3 sandbox for agent-driven physics and rendering experiments.

## Runtime Model

- `debugTool` is the debug executable.
- `interact_ui` is the editor UI surface.
- `kernal_all` is the non-UI debug host backend.
- `sdk` is the external agent/algorithm submission surface.
- An `agent` is the unit that carries algorithm packages, solver metadata, and intervention state.
- The current default workflow is: choose resource descriptors -> keep the draft prefilled -> create an agent -> run immediately.

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
- `algorithm_management` owns container-manifest loading and real runtime container creation.
- `algorithm_support` handles the package-level support / intervention payload flow.
- `capabilities/agent` holds the lightweight agent object and its attached algorithm metadata.
- `capabilities/algorithm_library` is reserved for concrete algorithm package capability bundles.
- `capabilities/sidecar` hosts optional sidecar capabilities such as mesh import/export.
- `agent_management` owns the runtime path for creating agents, keeping all created agents, and ticking them.
- `interact_ui` provides the manual agent composer and live debug UI panel without keeping a mesh resident in app state.
- `debugTool` is the executable entry point for interactive debugging.
- `sdk` exposes the external agent/algorithm submission entry points.

## Execution Workflow

- The interact UI and debug tool create agents from resource descriptors and algorithm bindings.
- The project does not yet have a formal algorithm execution engine.
- Current algorithm execution is still a temporary bring-up path: the main thread temporarily executes algorithm work through explicitly marked `temporaryTest` hooks.
- Physics owns the evolving algorithm-side state.
- Rendering reads the current algorithm-side data to draw points and edges.
- The default preset is prefilled so the user only needs to create the agent to start work.

## SDK Boundary

- External SDK users should include `src/sdk/sdk_kernel.h`.
- The SDK should not create UI.
- The SDK should not touch reflector or intervention hooks.

## Agent Composition

- Write each small algorithm capability in `capabilities/algorithm_library` with its own container manifest and package hooks.
- Build the final agent by attaching one or more algorithm support groups, solver config, and lightweight algorithm profiles to one agent object.
- Let upper layers assemble the agent creation spec, then let `agent_management` create and retain agents for runtime stepping while each `Agent` internally ticks its attached algorithm groups.
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

