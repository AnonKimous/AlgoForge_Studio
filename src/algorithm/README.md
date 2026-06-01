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

### GPU

- `physics_convolution_demo`
- Implementation: `gpu_physics_algorithm.*`
- Contract: `physics_convolution_gpu_algorithm_contract.*`

## Integration Flow

1. Upper layer selects backend and algorithm name.
2. Upper layer builds algorithm compliance descriptor from contract.
3. `AlgorithmMng` dispatches the pool.
4. Algorithm consumes only data that matches its own contract.

## Package Abstraction

`algorithm_package.h` defines simple/complex package codec hooks.

- Complex packages may provide custom conversion/reflection and debug signal collection.
- Simple packages can reuse default/common conversion behavior.
- Intervention packages can also provide codec, agent, and algorithm-side hooks for higher-level algorithms.
- Render-side packages now live under `orchestration_entity` and still present themselves as algorithm packages through the shared handle interface.
