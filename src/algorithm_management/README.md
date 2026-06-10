# Algorithm Management Module

## Rule

This strict trunk layer provides the algorithm manager surface.

- The only public entry header for code outside `src/algorithm_management` is `src/algorithm_management/algorithm_manager.h`.
- `src/algorithm_management/algorithm_types.h` and `src/algorithm_management/algorithm_container_manifest.h` are internal support headers.

## Integration Flow

1. Upper layers point the manager at an official container manifest.
2. `CreateAlgorithmContainersFromManifestName` resolves that manifest name under `src/capabilities/algorithm_library` and fails fast if it does not exist.
3. `CreateAlgorithmContainersFromManifestFile` creates real runtime containers from that manifest and reuses a cached template for repeated requests from the same manifest.
4. `CreateAlgorithmReflectorFromManifestName` and `CreateAlgorithmReflectorFromManifestFile` build reflector metadata from the same manifest when a reflector section exists.
5. `TryCreateAlgorithmReflectorFromAlgorithmName` is the optional algorithm-name shortcut: if no same-name manifest exists, it returns an empty reflector instead of failing.
6. Runtime container byte buffers are currently backed by `runtime_systems::MemoryManager`, which also serves as the monitoring hook for future allocator enforcement.
7. `job_gpu::Execute` is the job-system entry for GPU ticking. It keeps the `agent` side unaware of the runtime Vulkan executor implementation.
8. `job_cpu::Execute` is the matching job-system entry for CPU ticking for now, and the work still runs on the main thread through the package's temporary main-thread executor.

## Public Interface Rule

If a module outside `src/algorithm_management` needs algorithm-management types
or functions, it should include only:

- `algorithm_management/algorithm_manager.h`

Do not include internal algorithm-management headers from upper layers.
This rule is also enforced at compile time:

- the `algorithm_management` CMake target defines `ALGORITHM_MANAGEMENT_LAYER_INTERNAL_BUILD=1` only for its own compilation units
- `algorithm_manager.h` is the only header that legally opens the internal support headers for external consumers
- direct includes of `algorithm_types.h` or `algorithm_container_manifest.h` from outside the layer now fail with `#error`
- the public header exports the direct `Create...` and `TryCreate...` entrypoints only

## Package Abstraction

Package-level codec, decomposition, and intervention hooks are owned by
`capabilities/agent` and `codec` now.

- `CreateAlgorithmContainersFromManifestName` is the preferred public entry for runtime container creation.
- `CreateAlgorithmContainersFromManifestFile` is the lower-level entry used after a manifest path is known.
- Reflectors are also manifest-driven now; do not introduce descriptor-driven reflector construction on upper layers.
- `TryCreateAlgorithmReflectorFromAlgorithmName` is the only optional reflector entry. Strict manifest-name/file reflector creation still fails on malformed manifests.
- Capability-level composition may redirect containers between packages; `capabilities/agent` owns that composition, not the algorithm manager.
- GPU execution is mediated here as a management-layer service through `job_gpu`, but the Vulkan implementation remains internal to `runtime_systems`.
- CPU execution currently routes through `job_cpu` and still runs on the main thread.
- Reflectors and intervention metadata are debug-time support for `debugTool`; they are not part of the external SDK surface.

## Direction

This layer was intentionally simplified during decoupling, but the manager is
still the important center of the module.

If more responsibilities return here later, they should return through the
direct management surface rather than through scattered execution-specific
helpers.
