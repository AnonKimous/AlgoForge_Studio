# Layer Contract

This project keeps a constrained dependency chain for the strict main trunk and
separates intentionally cross-layer code into `src/capabilities`.

## Rules

- `common_data` is the shared base package.
- Main-trunk layers may only depend on the layer immediately below them.
- Each strict trunk layer exposes one public interface to the layer above it.
- Public interfaces are separated by provider. Do not reach into internal implementation headers from upper layers.
- `agent` sits above `algorithm_management` and owns the runtime agent object plus mount and submit entry points.
- Modules under `src/capabilities` are capability modules, not strict main-trunk hops.
- Capability modules may aggregate lower-level contracts for their own domain hooks, but they must not create upward dependencies into strict trunk layers.
- Sidecar-style capabilities must be linked explicitly by any consumer.

## Current Public Interfaces

- `common_data`: shared value headers; `common_data/common_data.h` is an optional aggregator, not the only entry.
- `agent`: `agent/agent.h`
- `algorithm_support`: internal implementation source group for package loading, plugin loading, support helpers, and intervention payload code
- `algorithm_management`: `algorithm_management/algorithm_manager.h` is the single public entrypoint
- `runtime_systems`: `runtime_systems/runtime_systems.h` is the public facade; `memory_manager.h` remains an internal support header
- `agent_management`: `agent_management/agent_management.h` is the public facade
- `debug_tool`: `debug_tool/debug_tool_host.h` for the host interface, `debug_tool/debug_tool_backend_runtime.h` for the backend runtime, and `debug_tool/debug_tool_frontend_panel.h` for the frontend panel
- `sdk`: `sdk/sdk.h` is the only public facade; `sdk_runtime_system.h` and `sdk_decomposer.h` are internal implementation headers

## Debug-Only Hooks

- Reflectors and intervention packages are debug-tool features.
- They may be used by `debugTool` for inspection, preview, and custom editor UI.
- The external SDK surface does not expose reflector or intervention APIs.
- If a consumer is building against `sdk`, it should treat those hooks as unavailable by design.

## Notes

- `SdlWindow` and `ImGuiVulkanRuntime` are internal implementation details of `runtime_systems`.
- Upper layers should not include implementation headers from lower layers unless those headers are the layer's public interface.
- `common_data` is the exception to the single-facade rule. It may be included as a loose package by specific header.
- `agent_management` coordinates mount/unmount/tick and forwards descriptor data to its agents, but it does not create containers itself.
- `sdk` talks directly to `agent_management`, must not include `algorithm_management` directly, and should not depend on `debug_tool` frontend code.
- For the algorithm-management layer, the only public interface header is `algorithm_management/algorithm_manager.h`.
- The algorithm-management manager creates real runtime containers from container manifests; missing manifests should fail immediately rather than degrade later at runtime.
- The same algorithm manifest may optionally create a reflector; reflector creation must also stay manifest-driven rather than descriptor-driven.
- For the runtime-systems layer, the only public interface header is `runtime_systems/runtime_systems.h`.
- For the agent-management layer, the only public interface header is `agent_management/agent_management.h`.
- `debugTool` is the debug executable surface. External SDK consumers should use `sdk/sdk.h` and should not depend on the UI layer.
