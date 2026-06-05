# Algorithm Management Module

## Rule

This strict trunk layer provides the algorithm manager surface.

- The only public entry header for code outside `src/algorithm_management` is `src/algorithm_management/algorithm_manager.h`.
- `src/algorithm_management/algorithm_types.h` and `src/algorithm_management/algorithm_container_manifest.h` are internal support headers.

## Integration Flow

1. Upper layers point the manager at an official container manifest.
2. `AlgorithmManager_CreateContainersFromManifestName` resolves that manifest name under `src/capabilities/algorithm_library` and fails fast if it does not exist.
3. `AlgorithmManager_CreateContainersFromManifestFile` creates real runtime containers from that manifest and reuses a cached template for repeated requests from the same manifest.
4. `AlgorithmManager_CreateReflectorFromManifestName` and `AlgorithmManager_CreateReflectorFromManifestFile` build reflector metadata from the same manifest when a reflector section exists.
5. `AlgorithmManager_TryCreateReflectorFromAlgorithmName` is the optional algorithm-name shortcut: if no same-name manifest exists, it returns an empty reflector instead of failing.

## Public Interface Rule

If a module outside `src/algorithm_management` needs algorithm-management types
or functions, it should include only:

- `algorithm_management/algorithm_manager.h`

Do not include internal algorithm-management headers from upper layers.
This rule is also enforced at compile time:

- the `algorithm_management` CMake target defines `ALGORITHM_MANAGEMENT_LAYER_INTERNAL_BUILD=1` only for its own compilation units
- `algorithm_manager.h` is the only header that legally opens the internal support headers for external consumers
- direct includes of `algorithm_types.h` or `algorithm_container_manifest.h` from outside the layer now fail with `#error`

## Package Abstraction

Package-level codec, decomposition, and intervention hooks are owned by
`capabilities/agent` and `codec` now.

- `AlgorithmManager_CreateContainersFromManifestName` is the preferred public entry for runtime container creation.
- `AlgorithmManager_CreateContainersFromManifestFile` is the lower-level entry used after a manifest path is known.
- Reflectors are also manifest-driven now; do not introduce descriptor-driven reflector construction on upper layers.
- `AlgorithmManager_TryCreateReflectorFromAlgorithmName` is the only optional reflector entry. Strict manifest-name/file reflector creation still fails on malformed manifests.
- Capability-level composition may redirect containers between packages; `capabilities/agent` owns that composition, not the algorithm manager.

## Direction

This layer was intentionally simplified during decoupling, but the manager is
still the important center of the module.

If more responsibilities return here later, they should return through the
algorithm manager surface rather than through scattered execution-specific
helpers.
