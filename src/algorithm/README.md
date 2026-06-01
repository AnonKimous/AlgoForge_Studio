# Algorithm Module

## Rule

This layer now only hosts the algorithm manager.

Concrete algorithm packages live in `algorithm_library`.

## Rule

Each ordinary algorithm package must provide:

1. Execution implementation.
2. Algorithm compliance descriptor contract.

## Current Ordinary Algorithms

### CPU

- `corotated_cpu`
- Implementation: `cpu_physics_algorithm.*`
- Contract: `corotated_cpu_algorithm_contract.*`
- `random_vertex_motion`
- Implementation: `random_vertex_motion_algorithm.*`
- Contract: `random_vertex_motion_algorithm_contract.*`

### GPU

- `physics_convolution_demo`
- Implementation: `gpu_physics_algorithm.*`
- Contract: `physics_convolution_gpu_algorithm_contract.*`

## Integration Flow

1. Upper layer selects backend and algorithm name.
2. Upper layer builds algorithm compliance descriptor from contract.
3. `AlgorithmMng` dispatches the pool.
4. The runtime request carries the final composite compliance descriptor for the instance.
5. Algorithm consumes only data that matches its own contract.
6. When an instance-level composite descriptor remaps container names, `AlgorithmMng_ResolveContainerName` resolves the final name that should be used for container lookup.

## Package Abstraction

`algorithm_package.h` defines simple/complex package codec hooks.

- Complex packages may provide custom conversion/reflection and debug signal collection.
- Simple packages can reuse default/common conversion behavior.
- Intervention packages can also provide codec, agent, and algorithm-side hooks for higher-level algorithms.
- Render-side packages now live under `orchestration_entity` and still present themselves as algorithm packages through the shared handle interface.
- `AlgorithmDataContract::container_aliases` is the package-scoped alias map used by instance-level composition.
- Entity-level pipelines may order multiple algorithm packages and redirect containers between them; the orchestration entity owns that composition, not the algorithm manager.
- `AlgorithmComplianceDescriptor::motion_radius` carries the default radius for instance-level random-vertex motion presets.
