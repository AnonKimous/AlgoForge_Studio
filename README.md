# physBased-render

This project is a layered Vulkan + SDL3 sandbox for agent-driven physics and rendering experiments.

## Runtime Model

- `interact_ui` is the main runtime UI.
- An `agent` is the unit that carries algorithm packages, solver metadata, and intervention state.
- The current default workflow is: load mesh -> keep the draft prefilled -> create an agent -> run immediately.

## Terminology Rule

- If a developer writes `实例` in Chinese, treat it as `agent` unless the context explicitly says otherwise.
- This rule applies to docs, comments, UI labels, and algorithm composition notes.

## Current Layering

- `common_data` holds shared mesh, math, input, and interaction types.
- `runtime_systems` owns windowing, ImGui, and Vulkan runtime support.
- `messaging` stays a transport layer.
- `algorithm_management` owns container-manifest loading and real runtime container creation.
- `codec` encodes and decodes compliance / intervention payloads.
- `capabilities/agent` holds the lightweight agent object and its attached algorithm metadata.
- `capabilities/algorithm_library` is reserved for concrete algorithm package capability bundles.
- `capabilities/sidecar` hosts optional sidecar capabilities such as mesh import/export.
- `agent_management` owns the runtime path for creating agents, keeping all created agents, and ticking them.
- `interact_ui` provides the manual agent composer and live debug UI.
- `app_orchestration` is the executable entry point.

## Execution Workflow

- The interact UI creates agents from the active mesh.
- The project does not yet have a formal algorithm execution engine.
- Current algorithm execution is still a temporary bring-up path: the main thread temporarily executes algorithm work through explicitly marked `temporaryTest` hooks.
- Physics owns the evolving vertex array.
- Rendering reads the current vertex array and the mesh topology arrays to draw points and edges.
- The default preset is prefilled so the user only needs to create the agent to start work.

## Agent Composition

- Write each small algorithm capability in `capabilities/algorithm_library` with its own container manifest and package hooks.
- Build the final agent by attaching one or more algorithm codec groups, solver config, and lightweight algorithm profiles to one agent object.
- Let upper layers assemble the agent creation spec, then let `agent_management` create and retain agents for runtime stepping while each `Agent` internally ticks its attached algorithm groups.
- The runtime does not try to validate the full graph; missing or incompatible bindings should fail at the point of use.

## Logs

- `DEVlog/2026-06-01_devlog.md` records the second June 1 modification pass.
- Older architecture notes live under `DEVlog/`.

