# Algorithm Module

## Rule

This layer now only hosts the algorithm manager.

- `AlgorithmMng` lives here under `src/algorithm/algorithm_mng.*`.

## Integration Flow

1. Upper layers build or load an algorithm container descriptor.
2. `AlgorithmMng_LoadContainerDescriptorFromJson*` loads official descriptor manifests.
3. When an agent-level composite descriptor remaps container names, `AlgorithmMng_ResolveContainerName` resolves the final name that should be used for container lookup.

## Package Abstraction

Package-level codec, decomposition, and intervention hooks are owned by `agent` and `codec` now.

- `AlgorithmMng_LoadContainerDescriptorFromJsonFile` and `AlgorithmMng_LoadContainerDescriptorFromJsonText` load container descriptors from official cJSON manifests.
- Agent-level composition may redirect containers between packages; the agent owns that composition, not the algorithm manager.
