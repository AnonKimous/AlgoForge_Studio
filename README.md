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
- `algorithm` manages execution and compliance descriptors.
- `algorithm_library` hosts concrete algorithm packages and their contracts.
- `codec` encodes and decodes compliance / intervention payloads.
- The agent module holds the lightweight agent object and its attached algorithm metadata.
- `agent_execute` owns the runtime path for creating agents, selecting the active one, and ticking it.
- `interact_ui` provides the manual agent composer and live debug UI.
- `app_orchestration` is the executable entry point.

## Execution Workflow

- The interact UI creates agents from the active mesh.
- Physics owns the evolving vertex array.
- Rendering reads the current vertex array and the mesh topology arrays to draw points and edges.
- The default preset is prefilled so the user only needs to create the agent to start work.

## Agent Composition

- Write each small algorithm in `algorithm_library` as a normal algorithm package with its own compliance descriptor.
- Build the final agent by attaching the algorithm package handles, solver config, and compliance descriptor to one agent object.
- Let upper layers assemble the agent launch spec, then let `agent_execute` bind one agent instance for runtime stepping.
- The runtime does not try to validate the full graph; missing or incompatible bindings should fail at the point of use.

## Logs

- `DEVlog/2026-06-01_devlog.md` records the second June 1 modification pass.
- Older architecture notes live under `DEVlog/`.

