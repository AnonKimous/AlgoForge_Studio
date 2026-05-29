# Current Version Summary

Date: `2026-05-29`

## Project state

This version of the project is in the middle of a hard architecture migration.
The goal of the migration is to make the codebase easier to reason about, easier to extend, and safer to move between different development machines.

The current code already reflects the new direction:

- module responsibilities are being split by layer
- cross-module synchronization is being lifted upward
- bus-based communication is replacing ad hoc direct coupling
- physics solver execution is being prepared for both CPU and GPU paths
- project-local layer namespaces are being used directly without an `echo::` prefix

The project currently builds successfully with:

- `cmake --build build --config Debug`
- target output: `build/Debug/min_vulkan_win32.exe`

## Current architecture direction

The codebase is now organized around these layer namespaces:

- `foundation`
- `runtime_systems`
- `data_protocol`
- `communication`
- `algorithm_contract`
- `algorithm_implementation`
- `core_services`
- `interaction_analysis`
- `agents`
- `app_orchestration`

The intended dependency direction is:

`foundation -> runtime_systems -> data_protocol -> communication -> algorithm_contract -> algorithm_implementation -> core_services -> interaction_analysis -> agents -> app_orchestration`

Rules currently being enforced:

- lower layers do not depend upward
- same-layer modules should not directly depend on each other
- same-layer coordination should be lifted to a higher layer
- synchronization logic should live in glue, orchestration, bus, or protocol code instead of leaking into core modules

See also:

- [layerReadme.md](/d:/gptsandbox/layerReadme.md)

## Important runtime structure

### Agents

The project is moving toward an agent-owned lifecycle model.
Modules that need lifecycle and ticking behavior are wrapped by agents.

Current examples:

- `WindowAgent`
- `SceneViewAgent`
- `RenderAgent`
- `ValidationAgent`
- `EditModeAgent`
- `GuideUiAgent`
- `PhysAgent`
- `IoBusAgent`
- `TriangleAnalysisAgent`

These live in:

- [module_agents.h](/d:/gptsandbox/src/app/module_agents.h)
- [module_agents.cpp](/d:/gptsandbox/src/app/module_agents.cpp)

### Communication

The project now contains a shared bus and a dedicated direct line abstraction.
Modules expose signal/data buffers, and higher-level code binds them once during initialization.

Important files:

- [io_buffers.h](/d:/gptsandbox/src/communication/io_buffers.h)
- [io_protocol.h](/d:/gptsandbox/src/communication/io_protocol.h)
- [io_protocol.cpp](/d:/gptsandbox/src/communication/io_protocol.cpp)
- [io_bus.h](/d:/gptsandbox/src/communication/io_bus.h)
- [io_bus.cpp](/d:/gptsandbox/src/communication/io_bus.cpp)

Current high-frequency direction:

- `GuideUi -> PhysAgent`: dedicated line
- validation actions: shared bus
- render runtime controls: shared bus

### Physics service

The old physics control flow is being concentrated into `PhysManager`.
Its job is to:

- hold physics solver configuration
- receive external packed signal/data buffers
- decode reflected input
- prepare algorithm requests
- dispatch to the selected algorithm path

Important files:

- [phys_manager.h](/d:/gptsandbox/src/physics/phys_manager.h)
- [phys_manager.cpp](/d:/gptsandbox/src/physics/phys_manager.cpp)
- [phys_manager_buffer_reflectors.h](/d:/gptsandbox/src/physics/phys_manager_buffer_reflectors.h)
- [phys_manager_buffer_reflectors.cpp](/d:/gptsandbox/src/physics/phys_manager_buffer_reflectors.cpp)

## CPU and GPU solver direction

The project is being prepared for two physics execution paths:

1. CPU solver path
2. GPU solver path

The CPU path remains the main functional path right now.
The GPU path already has framework code and a simple compute-oriented algorithm direction, including shader-based dispatch preparation.

Relevant algorithm areas:

- [src/algorithm](/d:/gptsandbox/src/algorithm)
- [physics_convolution.comp](/d:/gptsandbox/shaders/physics_convolution.comp)

The intended long-term rule is:

- physics submits an algorithm name plus reflected data requirements
- CPU and GPU implementations are separate
- if the requested backend does not provide the requested algorithm, the call should fail fast instead of silently falling back

## Rendering and camera direction

The renderer is being adapted around a more explicit camera model so the scene is not distorted by ImGui view resizing.
The current direction is:

- fixed camera position
- explicit world/view/projection handling
- scene sizing should remain stable when the UI layout changes

Relevant files:

- [scene_camera.h](/d:/gptsandbox/src/render/scene_camera.h)
- [scene_camera.cpp](/d:/gptsandbox/src/render/scene_camera.cpp)
- [vulkan_renderer.cpp](/d:/gptsandbox/src/render/vulkan_renderer.cpp)

## Interaction direction

Interaction has been separated into controllers under `interaction_analysis`.

Current controllers:

- `EditModeController`
- `GuideUiController`
- `PhysModeController`

Relevant files:

- [edit_mode_controller.h](/d:/gptsandbox/src/interaction/edit_mode_controller.h)
- [guide_ui_controller.h](/d:/gptsandbox/src/interaction/guide_ui_controller.h)
- [phys_mode_controller.h](/d:/gptsandbox/src/interaction/phys_mode_controller.h)

## Important migration changes already completed

### Namespace cleanup

The project-local namespace prefix `echo::` has been removed from the new architecture layers.
The project now uses layer namespaces directly, for example:

- `agents`
- `core_services`
- `interaction_analysis`
- `communication`
- `app_orchestration`

This keeps the code shorter and matches the current project preference.

### Hard migration progress

The following pieces have already been pulled into real layer namespaces and connected back into the build:

- agent layer
- app orchestration glue
- communication bus layer
- phys manager service layer
- interaction controllers
- validation bridge helpers

### Build validation

After the latest namespace and layering changes, the project was rebuilt successfully.

## Known issues and unfinished migration work

The project is not fully done migrating yet.
The current version should be treated as a stable transition point, not the final architecture endpoint.

Remaining important work:

- continue hard migration of `data_protocol` away from transition aliases
- keep removing leftover global-name compatibility surfaces
- continue tightening cross-layer ownership boundaries
- improve deployment robustness for new machines
- clean up remaining build environment assumptions

### Known environment warning

The current build completes successfully, but there is still a tail warning about `pwsh.exe` not being found.
This does not block the current binary output, but it should be cleaned up for smoother deployment on fresh Windows machines.

## Summary

This version is the first meaningful architecture-stabilization checkpoint after the project started moving toward:

- strict layering
- agent-driven lifecycle ownership
- bus/protocol-based module communication
- explicit CPU/GPU physics solver separation
- cleaner namespace and dependency boundaries

It is a good base for continuing the hard migration without going back to the older tightly coupled structure.
