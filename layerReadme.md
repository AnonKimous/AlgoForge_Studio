# Layer Readme

## Purpose

This file describes the repository structure that is actually present in the
workspace today.

Use it before:

- adding a new module
- moving code between folders
- wiring new runtime behavior into the app
- restoring older architecture ideas

When code and docs disagree, follow the code and update the docs.

## Core Rules

- Lower layers do not depend upward.
- `common_data` is shared in-memory data only.
- `common_data` is the one exception to the single-facade header rule.
- `runtime_systems` owns the SDL, ImGui, and Vulkan runtime shell.
- `codec` owns encode/decode and reflection helpers. It is not a general file-IO layer.
- `algorithm_management` is a strict main-trunk layer.
- `algorithm_management` owns container-manifest loading, manifest-name resolution, and runtime container creation helpers only.
- `kernal_all` owns the non-UI debug host backend and agent/runtime wiring.
- `interact_ui` is the editor-facing UI surface.
- `sdk` is the external agent/algorithm submission surface.
- `debugTool` owns startup wiring for the debug executable only.
- Modules under `src/capabilities` are capability modules, not strict main-trunk hops.
- Capability modules may aggregate lower-level contracts, but they must not introduce upward dependencies into strict trunk layers.
- Optional capabilities must be linked explicitly by any consumer.

## Current Module Graph

Strict main-trunk layering path:

`common_data -> codec -> algorithm_management -> agent -> agent_management -> kernal_all -> debugTool`

Runtime shell support path:

`common_data -> runtime_systems -> kernal_all -> debugTool`

UI path:

`common_data -> agent_management -> interact_ui(ui) -> debugTool`

Capability modules grouped under `src/capabilities`:

- `agent`
- `algorithm_library`
- `sidecar`

Current project-library dependency graph from `CMakeLists.txt`:

- `mesh_io -> common_data`
- `codec -> common_data`
- `algorithm_management -> common_data`
- `runtime_systems -> common_data`
- `agent -> common_data + algorithm_management + codec`
- `agent -> common_data + algorithm_management + codec`
- `agent_management -> common_data + agent`
- `kernal_all -> common_data + agent_management + runtime_systems`
- `interact_ui(ui) -> common_data + agent_management + runtime_systems + codec`
- `debugTool -> kernal_all + interact_ui(ui) + common_data`

Important note:

`codec` and `algorithm_management` are real trunk layers, but in the current
executable they still mostly provide contracts and helpers rather than a rich
live execution pipeline.

`capabilities/agent` is intentionally different: it is consumed by trunk code,
but it is a capability carrier rather than one strict hop in the layering path.

## Current Tree

```text
src/
├─ algorithm_management/
│  ├─ algorithm_manager.h
│  ├─ algorithm_container_manifest.h/.cpp
│  ├─ algorithm_types.h
│  └─ README.md
├─ capabilities/
│  ├─ README.md
│  ├─ agent/
│  │  ├─ agent.h/.cpp
│  │  └─ README.md
│  ├─ algorithm_library/
│  │  └─ README.md
│  └─ sidecar/
│     ├─ mesh_io.h/.cpp
│     └─ README.md
├─ common_data/
├─ codec/
│  ├─ codec_manager.h
│  ├─ codec_intervention.h
│  └─ codec_protocol.h
├─ runtime_systems/
├─ kernal_all/
├─ sdk/
├─ interact_ui/
└─ debug_tool/
```

## Public Interfaces

- `common_data`: specific headers or `common_data/common_data.h`
- `codec`: `codec/codec_manager.h` and `codec/codec_intervention.h`
- `algorithm_management`: `algorithm_management/algorithm_manager.h`
- `runtime_systems`: `runtime_systems/runtime_environment.h`
- `kernal_all`: no public interface; it is an internal debug backend target
- `interact_ui`: `interact_ui/interact_ui_runtime.h` and `interact_ui/interact_ui_panel.h`
- `sdk`: `sdk/sdk_kernel.h` and `sdk/sdk_decomposer.h`

## Module Roles

### `algorithm_management`

Strict trunk layer for manifest-driven runtime container creation.

It should:

- load official JSON manifests
- resolve manifest names from `capabilities/algorithm_library`
- create real runtime containers from a manifest
- cache per-manifest container templates for fast clone-and-clear reuse

It should not:

- own runtime execution state
- become a sidecar format layer
- turn back into a full algorithm runtime

Code outside `src/algorithm_management` should include only
`algorithm_management/algorithm_manager.h`.

### `capabilities/agent`

Cross-layer capability module for the lightweight `Agent` object and its package
hook contracts.

It may aggregate:

- algorithm-management container and manifest types
- codec hook contracts
- decomposer-provided resource reflection contracts
- shared interaction and common-data types

It should not:

- own outer runtime scheduling
- own the runtime shell
- become a hidden execution graph manager

### `capabilities/algorithm_library`

Reserved home for concrete algorithm package capability bundles.

Keep:

- package-local contracts
- package-local container manifests
- package-local hook bundles

Do not move manager responsibilities out of `algorithm_management` into this
directory.

### `capabilities/sidecar`

Optional external-format and adapter capabilities.

Current sidecar:

- `mesh_io`: OBJ mesh import/export on top of `common_data::Mesh`

### `kernal_all`

Debug backend layer for the executable. It owns:

- runtime lifetime
- the managed agent registry
- frame timing
- non-UI create/attach orchestration

It should not:

- render editor widgets directly
- become the SDK surface

### `interact_ui`

This layer is split into:

- interaction host runtime backend: `InteractUiRuntime` compiled into `kernal_all`
- editor-facing UI surface: `InteractUiPanel`

`InteractUiPanel` owns:

- editor-facing panels
- custom intervention UI integration

`debugTool` composes the backend and the UI surface.

### `sdk`

The SDK surface is for agent and algorithm submission.

It should:

- expose agent creation, destruction, algorithm mounting, resource mounting, algorithm submission, and unmounting
- avoid UI dependencies
- avoid reflector/intervention UI surfaces

## Current Runtime Flow

1. `main.cpp` builds a default triangle mesh.
2. `InteractUiRuntime::Init` initializes `RuntimeEnvironment`.
3. `debugTool` binds `InteractUiPanel` to the runtime host and sets the draw callback.
4. `RuntimeEnvironment` drives the SDL and ImGui frame loop.
5. `InteractUiPanel::Draw` calls into the managed agent registry through `IInteractUiHost`.
6. `AgentTicker` builds macro tick context and lets `Agent` tick its attached algorithm codec groups.

## Quick Guidance For AI Agents

When changing code:

1. Start from this file and `src/README.md`.
2. Decide whether the new behavior belongs in the strict trunk or under `src/capabilities`.
3. Keep `runtime_systems` behind `RuntimeEnvironment`.
4. Keep packet transport as shared packet structs in `common_data`.
5. Keep manifest loading and runtime container creation helpers in `algorithm_management`.
6. Keep cross-layer package hooks in `capabilities/agent`.
7. Keep optional adapters in `capabilities/sidecar`.
8. Keep runtime binding in `kernal_all`.
9. Keep interaction-host behavior in `interact_ui` runtime backend and editor behavior in `interact_ui` panel.
10. Do not claim a full execution pipeline exists unless you also implement it.
