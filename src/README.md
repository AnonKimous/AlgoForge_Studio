# Layer Contract

This project keeps a constrained dependency chain for the strict main trunk and
separates intentionally cross-layer code into `src/capabilities`.

## Rules

- `common_data` is the shared base package.
- Main-trunk layers may only depend on the layer immediately below them.
- Each strict trunk layer exposes one public interface to the layer above it.
- Public interfaces are separated by provider. Do not reach into internal implementation headers from upper layers.
- Modules under `src/capabilities` are capability modules, not strict main-trunk hops.
- Capability modules may aggregate lower-level contracts for their own domain hooks, but they must not create upward dependencies into strict trunk layers.
- Sidecar-style capabilities must be linked explicitly by any consumer.

## Current Public Interfaces

- `common_data`: shared value headers; `common_data/common_data.h` is an optional aggregator, not the only entry.
- `codec`: `codec/codec_manager.h`
- `algorithm_management`: `algorithm_management/algorithm_manager.h`
- `runtime_systems`: `runtime_systems/runtime_environment.h`
- `agent_management`: `agent_management/agent_manager.h`
- `interact_ui`: `interact_ui/interact_ui_runtime.h` (interaction host surface)

## Notes

- `SdlWindow` and `ImGuiVulkanRuntime` are internal implementation details of `runtime_systems`.
- Upper layers should not include implementation headers from lower layers unless those headers are the layer's public interface.
- `common_data` is the exception to the single-facade rule. It may be included as a loose package by specific header.
- For the algorithm-management layer, the only public interface header is `algorithm_management/algorithm_manager.h`.
- The algorithm-management manager creates real runtime containers from container manifests; missing manifests should fail immediately rather than degrade later at runtime.
- The same algorithm manifest may optionally create a reflector; reflector creation must also stay manifest-driven rather than descriptor-driven.
- For the runtime-systems layer, internal window/render headers are not public.
- For the agent-management layer, the only public interface header is `agent_management/agent_manager.h`.
