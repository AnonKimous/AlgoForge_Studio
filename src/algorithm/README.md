# Algorithm Module

## Rule

This layer now only hosts the algorithm manager.

- `AlgorithmMng` lives here under `src/algorithm/algorithm_mng.*`.

## Integration Flow

1. Upper layers build or load an algorithm container descriptor.
2. `AlgorithmMng_LoadContainerDescriptorFromJson*` loads official descriptor manifests.
3. When an agent-level composite descriptor remaps container names, `AlgorithmMng_ResolveContainerName` resolves the final name that should be used for container lookup.

## Package Abstraction

`algorithm_package.h` defines simple/complex package codec hooks.

- Package codecs may provide custom conversion, reflection, and debug signal collection.
- Simple packages can reuse default/common conversion behavior.
- Intervention packages can also provide codec, agent, and algorithm-side hooks for higher-level orchestration.
- `AlgorithmDataContract::container_aliases` is the package-scoped alias map used by agent-level composition.
- `IAlgorithmPackageCodec::ReflectReadableParameters` exposes a human-friendly summary of the container descriptor.
- `IAlgorithmPackageDecomposer::ReflectDecomposition` exposes which runtime resources the decomposition needs.
- `AlgorithmMng_LoadContainerDescriptorFromJsonFile` and `AlgorithmMng_LoadContainerDescriptorFromJsonText` load container descriptors from official cJSON manifests.
- Agent-level composition may redirect containers between packages; the agent owns that composition, not the algorithm manager.
