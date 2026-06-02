# Algorithm Module

## Rule

This layer now only hosts the algorithm manager.

Concrete algorithm packages live in `algorithm_library`.

## Rule

Each ordinary algorithm package must provide:

1. Execution implementation.
2. Algorithm container descriptor contract.

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
2. Upper layer builds algorithm container descriptor from contract.
3. `AlgorithmMng` dispatches the pool.
4. The runtime request carries the final composite container descriptor for the agent.
5. Algorithm consumes only data that matches its own contract.
6. When an agent-level composite descriptor remaps container names, `AlgorithmMng_ResolveContainerName` resolves the final name that should be used for container lookup.

## Package Abstraction

`algorithm_package.h` defines simple/complex package codec hooks.

- Package codecs may provide custom conversion, decomposition, reflection, and debug signal collection.
- Simple packages can reuse default/common conversion behavior.
- Intervention packages can also provide codec, agent, and algorithm-side hooks for higher-level algorithms.
- Render-side packages now live under the agent-composition layer and still present themselves as algorithm packages through the shared handle interface.
- `AlgorithmDataContract::container_aliases` is the package-scoped alias map used by agent-level composition.
- `IAlgorithmPackageCodec::BuildBoundResources` lets a package describe which runtime resources the upper layer should bind for it.
- `IAlgorithmPackageCodec::ReflectReadableParameters` exposes a human-friendly summary of the container descriptor.
- `IAlgorithmPackageCodec::ReflectDescriptorShape` exposes the required resources together with the descriptor shape for orchestration or UI inspection.
- `AlgorithmMng_LoadContainerDescriptorFromJsonFile` and `AlgorithmMng_LoadContainerDescriptorFromJsonText` load container descriptors from official cJSON manifests.
- Agent-level pipelines may order multiple algorithm packages and redirect containers between them; the agent owns that composition, not the algorithm manager.
