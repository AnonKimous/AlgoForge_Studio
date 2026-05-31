# Algorithm Module

## Rule

Each algorithm package must provide:

1. Execution implementation.
2. Algorithm compliance descriptor contract.

## Current Algorithms

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
3. PhysAgent builds request and dispatches pipeline.
4. Algorithm consumes only data that matches its own contract.

## Package Abstraction

`algorithm_package.h` defines simple/complex package codec hooks.

- Complex packages may provide custom conversion/reflection and debug signal collection.
- Simple packages can reuse default/common conversion behavior.
