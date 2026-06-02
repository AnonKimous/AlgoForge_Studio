# physBased-render

This project is a layered Vulkan + SDL3 sandbox for agent-driven physics and rendering experiments.

## Runtime Model

- `interact_ui` is the main runtime UI.
- An `agent` is the unit that carries algorithm packages, solver metadata, and intervention state.
- A shared agent can mount both render and physics roles.
- The current default workflow is: load mesh -> keep the draft prefilled -> create the shared agent -> run immediately.

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
- The agent module describes ordered packages, aliases, and container routing.
- `agent_execute` owns the runtime wrappers for render, physics, and agent submission.
- `interact_ui` provides the manual agent composer and live debug UI.
- `app_orchestration` is the executable entry point.

## Execution Workflow

- The interact UI creates agents from the active mesh.
- Physics owns the evolving vertex array.
- Rendering reads the current vertex array and the mesh topology arrays to draw points and edges.
- The default preset is prefilled so the user only needs to create the agent to start work.

## Agent Composition

- Write each small algorithm in `algorithm_library` as a normal algorithm package with its own compliance descriptor.
- Define the low-level contract first: which containers it reads, which arrays it allocates, and which names it exposes.
- Build the agent-level composite in the agent module by ordering packages and defining container aliases and routes.
- Use the agent-level compliance descriptor as the final descriptor handed to `algorithm` / `algorithm_mng` for container allocation.
- Let the agent remap a package container name to a final composite name through aliases; the runtime does not try to validate the whole graph.
- If the structure is complex, compose it with the helper script under `tools/algorithm_descriptor_composer/` and export the merged descriptor for the agent.
- The intended pattern is: small algorithms stay small, the agent stitches them together into one composite pipeline.

## Logs

- `DEVlog/2026-06-01_devlog.md` records the second June 1 modification pass.
- Older architecture notes live under `DEVlog/`.

