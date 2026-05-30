# Algorithm Module

## Rule

Every algorithm in this folder must provide two things together:

1. Its execution implementation.
2. Its algorithm compliance descriptor data structures for the codec and manager.

Do not only write the compute logic and leave the reflection layout in upper modules.

## Required File Pattern

For each algorithm, add:

1. `*_algorithm.cpp`
2. `*_algorithm.h`
3. `*_algorithm_contract.cpp`
4. `*_algorithm_contract.h`

The `*_contract.*` files are the only place where the algorithm's compliance descriptor should be declared.
Do not put extra algorithm-specific helper value structs in these files unless they are truly part of the public contract.

## CPU And GPU Separation

CPU and GPU contracts must stay strictly separated.

1. CPU contract files may only describe CPU job style arrays, temporary registers, and caches.
2. GPU contract files may only describe GPU shader style arrays, temporary registers, and caches.
3. A CPU algorithm must not expose GPU descriptor-style data blocks.
4. A GPU algorithm must not expose CPU job packet data blocks.

## Current Algorithms

### CPU

1. `legacy_corotated_cpu`
Implementation:
[`cpu_physics_algorithm.cpp`](/d:/gptsandbox/src/algorithm/cpu_physics_algorithm.cpp)

Contract:
[`legacy_corotated_cpu_algorithm_contract.h`](/d:/gptsandbox/src/algorithm/legacy_corotated_cpu_algorithm_contract.h)
[`legacy_corotated_cpu_algorithm_contract.cpp`](/d:/gptsandbox/src/algorithm/legacy_corotated_cpu_algorithm_contract.cpp)

2. `triangle_orientation_cpu`
Implementation:
[`triangle_orientation_cpu_algorithm.cpp`](/d:/gptsandbox/src/algorithm/triangle_orientation_cpu_algorithm.cpp)

Contract:
[`triangle_orientation_cpu_algorithm_contract.h`](/d:/gptsandbox/src/algorithm/triangle_orientation_cpu_algorithm_contract.h)
[`triangle_orientation_cpu_algorithm_contract.cpp`](/d:/gptsandbox/src/algorithm/triangle_orientation_cpu_algorithm_contract.cpp)

Job dispatch placeholder:
[`cpu_job_scheduler.h`](/d:/gptsandbox/src/algorithm/cpu_job_scheduler.h)
[`cpu_job_scheduler.cpp`](/d:/gptsandbox/src/algorithm/cpu_job_scheduler.cpp)

### GPU

1. `physics_convolution_demo`
Implementation:
[`gpu_physics_algorithm.cpp`](/d:/gptsandbox/src/algorithm/gpu_physics_algorithm.cpp)

Contract:
[`physics_convolution_gpu_algorithm_contract.h`](/d:/gptsandbox/src/algorithm/physics_convolution_gpu_algorithm_contract.h)
[`physics_convolution_gpu_algorithm_contract.cpp`](/d:/gptsandbox/src/algorithm/physics_convolution_gpu_algorithm_contract.cpp)

## Integration Flow

1. Upper layer selects backend and algorithm name.
2. Upper layer asks the algorithm contract for `Create...AlgorithmComplianceDescriptor`.
3. `PhysManager` inspects the algorithm compliance descriptor.
4. Pipeline dispatches by backend and algorithm name.
5. Algorithm implementation only consumes data that matches its own contract.

## What Upper Layers Should Not Do

Upper layers should not manually assemble raw reflection requests inline for a specific algorithm.

Bad:

- Build a one-off reflection request directly in `main.cpp` for a concrete algorithm.

Good:

- Call `CreateLegacyCorotatedCpuAlgorithmComplianceDescriptor(...)`
- Call `CreatePhysicsConvolutionGpuAlgorithmComplianceDescriptor(...)`
