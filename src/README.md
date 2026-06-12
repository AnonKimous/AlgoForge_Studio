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
- `algorithm_support`: `algorithm_support/algorithm_support_manager.h`, `algorithm_support/algorithm_protocol.h`
- `algorithm_management`: `algorithm_management/algorithm_manager.h`
- `runtime_systems`: `runtime_systems/runtime_environment.h`, `runtime_systems/memory_manager.h`
- `agent_management`: `agent_management/agent_manager.h`
- `interact_ui`: `interact_ui/interact_ui_runtime.h` for the interaction host surface, `interact_ui/interact_ui_panel.h` for the editor-facing UI
- `sdk`: `sdk/sdk_kernel.h` for the external agent/algorithm submission surface and `sdk/sdk_decomposer.h` for the decomposer helper surface

## Debug-Only Hooks

- Reflectors and intervention packages are debug-tool features.
- They may be used by `debugTool` for inspection, preview, and custom editor UI.
- The external SDK surface does not expose reflector or intervention APIs.
- If a consumer is building against `sdk`, it should treat those hooks as unavailable by design.

## Notes

- `SdlWindow` and `ImGuiVulkanRuntime` are internal implementation details of `runtime_systems`.
- Upper layers should not include implementation headers from lower layers unless those headers are the layer's public interface.
- `common_data` is the exception to the single-facade rule. It may be included as a loose package by specific header.
- For the algorithm-management layer, the only public interface header is `algorithm_management/algorithm_manager.h`.
- The algorithm-management manager creates real runtime containers from container manifests; missing manifests should fail immediately rather than degrade later at runtime.
- The same algorithm manifest may optionally create a reflector; reflector creation must also stay manifest-driven rather than descriptor-driven.
- For the runtime-systems layer, internal window/render headers are not public.
- For the agent-management layer, the only public interface header is `agent_management/agent_manager.h`.
- `debugTool` is the debug executable surface. External SDK consumers should use `sdk/sdk_kernel.h` and should not depend on the UI layer.
