# Layer Readme

## Purpose

This file describes the current framework shape after the recent cleanup passes.
It is written for both human developers and AI agents.

Use it as the first architecture reference before adding new files, moving code, or restoring deleted legacy paths.

## Core Rules

- Lower layers do not depend upward.
- Same-layer modules do not directly depend on each other.
- Cross-cutting coordination is lifted to upper orchestration layers.
- `common_data` keeps shared in-memory data only.
- `algorithm` keeps descriptor and manager concerns only.
- `agent` is a lightweight carrier, not a runtime executor.
- `agent_execute` is a runtime binding and signal-gating layer, not an algorithm assembly layer.
- `interact_ui` is UI only. It should not own algorithm assembly logic.
- Sidecar capability modules stay outside the primary layer chain and must be linked explicitly by users.

## Primary Layer Chain

Current intended direction:

`common_data -> runtime_systems -> messaging -> algorithm -> codec -> agent -> agent_execute -> interact_ui -> app_orchestration`

## Sidecar Rule

Sidecar capability modules do not belong to the primary layer chain.

They are used for optional capabilities such as:

- file formats
- persistence
- third-party adapters

Current sidecar module:

- `mesh_io`

Rules:

- A sidecar module may depend on stable lower-layer data structures.
- Business layers must link sidecar modules directly.
- Do not hide sidecar usage behind unrelated business layers.
- One external capability should have one clear owner module.

## Current Tree

```text
src/
├─ common_data/
│  ├─ mesh.h/.cpp
│  ├─ vector_types.h
│  ├─ input_state.h
│  ├─ viewport_transform.h/.cpp
│  ├─ interaction/
│  │  └─ interaction_signals.h
│  ├─ render_algorithm/
│  │  ├─ camera_math.h
│  │  └─ viewport_math.h
│  ├─ phys_algorithm/
│  │  └─ velocity_math.h
│  └─ physics/
│     └─ physics_types.h
│
├─ runtime_systems/
│  ├─ window/
│  │  ├─ window_handle.h
│  │  ├─ window.h
│  │  └─ sdl_window.h/.cpp
│  └─ render/
│     ├─ imgui_vulkan_runtime.h/.cpp
│     └─ vma_impl.cpp
│
├─ messaging/
│  ├─ io_buffers.h
│  └─ io_bus.h/.cpp
│
├─ algorithm/
│  ├─ algorithm_types.h
│  ├─ algorithm_package.h
│  ├─ algorithm_container_manifest.h/.cpp
│  ├─ algorithm_mng.h
│  └─ README.md
│
├─ codec/
│  └─ codec_manager.h/.cpp
│
├─ agent/
│  ├─ agent.h/.cpp
│  └─ README.md
│
├─ agent_execute/
│  ├─ agent_ticker.h/.cpp
│  ├─ agent_execute_runtime.h/.cpp
│  └─ README.md
│
├─ interact_ui/
│  ├─ interact_ui_runtime.h/.cpp
│  └─ README.md
│
├─ sidecar/
│  ├─ mesh_io.h/.cpp
│  └─ README.md
│
└─ app_orchestration/
   └─ main.cpp
```

## Layer Roles

### `common_data`

Shared in-memory structures and math helpers.

Allowed here:

- mesh data
- vector types
- input state
- generic interaction signals
- low-level math helpers

Not allowed here:

- algorithm manager logic
- UI workflow state
- runtime binding logic
- file IO

### `runtime_systems`

Low-level runtime backends such as SDL windowing, ImGui runtime, and Vulkan support.

### `messaging`

Low-level packet and buffer transport only.

It should move data, not interpret business meaning.

### `algorithm`

Descriptor and manager layer.

Current responsibilities:

- algorithm container descriptor types
- manifest loading from official JSON shape
- container alias resolution
- manager-facing helper entrypoints
- package hook interfaces

Important rule:

`algorithm` should only reason about containers and descriptor contracts.
It should not grow back domain-specific execution semantics such as physics, render, acceleration, or solver workflows.

### `codec`

Encode/decode layer for protocol payloads and intervention packets.

It is not a general file import/export layer.

### `agent`

Lightweight carrier object.

It may hold:

- agent name
- algorithm name
- attached package handles
- intervention package handle
- compliance descriptor

It does not own ticking or UI.

### `agent_execute`

Runtime binding and signal-gating layer.

Current responsibilities:

- hold the bound agent
- gate execution by agent signals
- hold the current intervention request

It should not:

- assemble algorithms
- own physics runtime state
- own legacy solver request structs

### `interact_ui`

Editor-facing UI layer.

Current responsibilities:

- load mesh files through `mesh_io`
- display current status
- inspect bound-agent state

It should not:

- build algorithm launch specs
- choose solver internals
- encode algorithm intervention packets itself

### `app_orchestration`

Top-level executable entry.

This is where startup wiring belongs.

## Current Simplifications

The following legacy areas have already been removed or sharply reduced:

- `src/agents`
- `algorithm_library`
- physics/gpu algorithm implementation files under `src/algorithm`
- old runtime-side algorithm assembly UI
- `common_data/interaction/interaction_state.h`
- `agent_execute` request/result assembly logic

## Current Transitional Notes

Some legacy-flavored names still exist and may be reduced later, for example:

- `common_data/physics/physics_types.h`
- `common_data/phys_algorithm/velocity_math.h`
- some mesh-oriented and volume-oriented hooks in `algorithm_package.h`

These files are no longer part of the main execution path, but they still carry old domain naming.

When touching them:

- prefer deletion over expansion
- prefer neutral container language over domain language
- do not reintroduce upward dependencies

## Quick Guidance For AI Agents

When adding or changing code:

1. Start from this file.
2. Check whether the target belongs to the primary layer chain or a sidecar module.
3. Prefer deleting legacy wrappers instead of renaming and keeping them.
4. Do not reintroduce `algorithm_library`.
5. Do not reintroduce `src/agents`.
6. Do not put UI workflow state back into `common_data`.
7. Do not put algorithm assembly back into `interact_ui`.
8. Do not put execution-domain semantics back into `algorithm`.
