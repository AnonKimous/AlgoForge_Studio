# physBased-render

This project is a layered Vulkan + SDL3 sandbox for entity-driven physics and rendering experiments.

## Runtime Model

- `editor_ui` is the main runtime UI.
- An `entity` is the unit that carries algorithm packages, agent bindings, solver metadata, and intervention state.
- A shared entity can mount both render and physics roles.
- The current default workflow is: load mesh -> keep the draft prefilled -> create the shared entity -> run immediately.

## Terminology Rule

- If a developer writes `实例` in Chinese, treat it as `entity` unless the context explicitly says otherwise.
- In this repository, `instance` is not a separate runtime primitive; `entity` is the canonical term.
- This rule applies to docs, comments, UI labels, and algorithm composition notes.

## Current Layering

- `common_data` holds shared mesh, math, input, and interaction types.
- `runtime_systems` owns windowing, ImGui, and Vulkan runtime support.
- `messaging` stays a transport layer.
- `algorithm` manages execution and compliance descriptors.
- `algorithm_library` hosts concrete algorithm packages and their contracts.
- `codec` encodes and decodes compliance / intervention payloads.
- `orchestration_entity` describes the instance-level composition of ordered packages, aliases, and container routing.
- `agents` owns the runtime wrappers for render and physics.
- `entity_interaction` provides the manual entity composer backend.
- `editor_ui` provides the manual entity composer and live debug UI.
- `app_orchestration` is the executable entry point.

## Entity Workflow

- The editor UI creates entities from the active mesh.
- Physics owns the evolving vertex array.
- Rendering reads the current vertex array and the mesh topology arrays to draw points and edges.
- The default preset is prefilled so the user only needs to create the entity to start work.

## Instance-Level Algorithms

- Write each small algorithm in `algorithm_library` as a normal algorithm package with its own compliance descriptor.
- Define the low-level contract first: which containers it reads, which arrays it allocates, and which names it exposes.
- Build the entity-level composite in `orchestration_entity` by ordering packages and defining container aliases and routes.
- Use the entity-level compliance descriptor as the final descriptor handed to `algorithm` / `algorithm_mng` for container allocation.
- Let the entity remap a package container name to a final composite name through aliases; the runtime does not try to validate the whole graph.
- If the structure is complex, compose it with the helper script under `tools/algorithm_descriptor_composer/` and export the merged descriptor for the entity.
- The intended pattern is: small algorithms stay small, the entity stitches them together into one instance-level pipeline.

## Build

Use the project script:

```bat
build_msvc.cmd build
```

If you need to configure and build in one step:

```bat
build_msvc.cmd
```

## Logs

- `DEVlog/2026-06-01_devlog.md` records the second June 1 modification pass.
- Older architecture notes live under `DEVlog/`.
