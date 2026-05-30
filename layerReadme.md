# Layer Readme

## Core rules

- Lower layers must not depend back upward.
- Same-layer modules must not directly depend on each other.
- If same-layer collaboration is required, the dependency must be lifted into the next higher layer as synchronization, protocol dispatch, or orchestration.
- Algorithm containers and layouts are defined by algorithm contracts; upper layers must implement those requirements.

## Namespace policy

- `foundation`
- `runtime_systems`
- `data_protocol`
- `messaging`
- `algorithm_contract`
- `algorithm_implementation`
- `core_services`
- `interaction_analysis`
- `agents`
- `app_orchestration`

Current code now exposes these layer namespaces from the main architecture boundary headers first.
Global names are still present for now so the project keeps building while we continue the deeper migration.

## 10-layer structure

### 1. Foundation
Namespace: `foundation`

Files:

- `src/math/common/vector_types.h`
- `src/math/phys_algorithm/velocity_math.h`
- `src/math/render_algorithm/camera_math.h`
- `src/math/render_algorithm/viewport_math.h`
- `src/viewport_transform.h`
- `src/viewport_transform.cpp`
- `src/window/input_state.h`
- `src/window/window_handle.h`

Responsibility:

- math primitives
- math helper functions
- low-level transforms
- no business semantics

### 2. Runtime Systems
Namespace: `runtime_systems`

Files:

- `src/algorithm/cpu_job_scheduler.h`
- `src/algorithm/cpu_job_scheduler.cpp`
- `src/algorithm/data_reflection.h`
- `src/algorithm/data_reflection.cpp`
- `src/render/vma_impl.cpp`
- `src/window/window.h`
- `src/window/sdl_window.h`
- `src/window/sdl_window.cpp`

Responsibility:

- future job system
- reflection execution helpers
- future memory/runtime allocation infrastructure
- platform window runtime
- Vulkan memory backend hookup

Note:

- future memory system and reflection runtime should continue to live here
- algorithm contracts may describe required memory, but runtime systems execute the allocation / packing / reflection work

### 3. Data / State / Protocol
Namespace: `data_protocol`

Files:

- `src/mesh.h`
- `src/mesh.cpp`
- `src/triangle_orientation_state.h`
- `src/physics/physics_types.h`
- `src/physics/gpu_physics_types.h`
- `src/algorithm/algorithm_types.h`
- `src/interaction/interaction_state.h`
- `src/validation_layer/validation_snapshot.h`
- `src/validation_layer/validation_actions.h`
- `src/communication/io_buffers.h`
- `src/communication/io_protocol.h`

Responsibility:

- shared states
- protocol packet definitions
- algorithm-visible data definitions
- render/interaction/validation shared data language

### 4. Messaging
Namespace: `messaging`

Files:

- `src/communication/io_bus.h`
- `src/communication/io_bus.cpp`
- `src/communication/io_protocol.cpp`

Responsibility:

- shared bus transport
- protocol translation
- packet routing / dispatch
- no business execution logic

Rule:

- modules prepare their own receive buffers
- the messaging layer only reads/writes common packets
- endpoint binding happens during initialization, not every frame

### 5. Algorithm Contract
Namespace: `algorithm_contract`

Files:

- `src/algorithm/legacy_corotated_cpu_algorithm_contract.h`
- `src/algorithm/legacy_corotated_cpu_algorithm_contract.cpp`
- `src/algorithm/physics_convolution_gpu_algorithm_contract.h`
- `src/algorithm/physics_convolution_gpu_algorithm_contract.cpp`
- `src/algorithm/triangle_orientation_cpu_algorithm_contract.h`
- `src/algorithm/triangle_orientation_cpu_algorithm_contract.cpp`
- `src/algorithm/README.md`

Responsibility:

- algorithm-required container/layout descriptions
- contract-visible data formats
- container requirements imposed upward

Rule:

- upper layers must satisfy these contracts
- algorithm implementations must not weaken them ad hoc

### 6. Algorithm Implementation
Namespace: `algorithm_implementation`

Files:

- `src/algorithm/cpu_physics_algorithm.h`
- `src/algorithm/cpu_physics_algorithm.cpp`
- `src/algorithm/gpu_physics_algorithm.h`
- `src/algorithm/gpu_physics_algorithm.cpp`
- `src/algorithm/physics_algorithm_pipeline.h`
- `src/algorithm/physics_algorithm_pipeline.cpp`
- `src/algorithm/triangle_orientation_cpu_algorithm.h`
- `src/algorithm/triangle_orientation_cpu_algorithm.cpp`

Responsibility:

- CPU algorithm bodies
- GPU algorithm bodies
- pipeline dispatch by backend and algorithm name

### 7. Core Services
Namespace: `core_services`

Files:

- `src/physics/phys_manager.h`
- `src/physics/phys_manager.cpp`
- `src/physics/phys_manager_buffer_reflectors.h`
- `src/physics/phys_manager_buffer_reflectors.cpp`
- `src/physics/physics_module.h`
- `src/physics/physics_solver.h`
- `src/physics/physics_solver.cpp`
- `src/render/renderer.h`
- `src/render/render_module.h`
- `src/render/scene_camera.h`
- `src/render/scene_camera.cpp`
- `src/render/vulkan_renderer.cpp`
- `src/validation_layer/validation_layer.h`
- `src/validation_layer/validation_layer.cpp`
- `src/validation_layer/validation_layer_stub.cpp`
- `src/validation_layer/validation_script.h`
- `src/validation_layer/validation_script.cpp`

Responsibility:

- physics service
- render service
- validation service

Rule:

- services in this layer do not depend on each other directly
- service-to-service coordination must be lifted into `Agents` or `App Orchestration`

### 8. Interaction / Analysis
Namespace: `interaction_analysis`

Files:

- `src/interaction/edit_mode_controller.h`
- `src/interaction/edit_mode_controller.cpp`
- `src/interaction/guide_ui_controller.h`
- `src/interaction/guide_ui_controller.cpp`
- `src/interaction/phys_mode_controller.h`
- `src/interaction/phys_mode_controller.cpp`
- `src/interaction/interaction_module.h`
- `src/validation_bridge/validation_bridge.h`
- `src/validation_bridge/validation_bridge.cpp`
- `src/validation_bridge/validation_action_bridge.h`
- `src/validation_bridge/validation_action_bridge.cpp`

Responsibility:

- user intent interpretation
- guide editing interpretation
- analysis result shaping
- validation snapshot shaping

Rule:

- this layer may depend downward on core services and shared state
- it must not take over app-level orchestration

### 9. Agents
Namespace: `agents`

Files:

- `src/app/agents.h`
- `src/app/agents.cpp`

Responsibility:

- lifecycle wrappers
- endpoint exposure
- protocol resolving
- stable tick/init/destroy entry points

### 10. App Orchestration
Namespace: `app_orchestration`

Files:

- `src/main.cpp`
- `src/glue/app_frame_sync_glue.h`
- `src/glue/app_frame_sync_glue.cpp`
- `src/glue/guide_ui_frame_relay_on_phys_agent_buffers.h`
- `src/glue/guide_ui_frame_relay_on_phys_agent_buffers.cpp`

Responsibility:

- bind buses and dedicated lines
- publish protocol packets
- trigger top-level synchronization
- own cross-module timing decisions

## Dependency direction

Recommended direction:

`Foundation -> Runtime Systems -> Data / State / Protocol -> Communication -> Algorithm Contract -> Algorithm Implementation -> Core Services -> Interaction / Analysis -> Agents -> App Orchestration`

Important notes:

- `Communication` may depend on `Data / State / Protocol`, but not on business services.
- `Algorithm Implementation` depends on `Algorithm Contract`, never the other way around.
- `Core Services` may call algorithms, but algorithm code must not depend back on services.
- same-layer service collaboration must be lifted upward.
- `Agents` may expose buffers from lower modules, but should not become business-logic owners.

## Current high-frequency bus policy

- `GuideUi -> PhysAgent`: dedicated line
- `Validation -> RenderAgent / PhysAgent`: shared bus with validation-action protocol
- `Render runtime controls -> PhysAgent`: shared bus with runtime-control protocol
