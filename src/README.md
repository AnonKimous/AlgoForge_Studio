# Layer Contract

This project keeps a constrained dependency chain for the main trunk.

## Rules

- `common_data` is the shared base package.
- Main-trunk layers may only depend on the layer immediately below them.
- Each layer exposes one public interface to the layer above it.
- Public interfaces are separated by provider. Do not reach into internal implementation headers from upper layers.
- `agent`, `algorithm`, and `intervention` capability code are exempt from the trunk restriction. They may aggregate lower-level types as needed for their own domain hooks.
- Sidecar modules stay outside the main trunk and must be linked explicitly by any consumer.

## Current Public Interfaces

- `common_data`: value types, mesh helpers, input types.
- `messaging`: buffer and bus primitives.
- `codec`: codec manager and wire-format helpers.
- `runtime_systems`: `RuntimeEnvironment`.
- `agent_execute`: `AgentExecuteRuntime`.
- `interact_ui`: `InteractUiRuntime`.

## Notes

- `SdlWindow` and `ImGuiVulkanRuntime` are internal implementation details of `runtime_systems`.
- Upper layers should not include implementation headers from lower layers unless those headers are the layer's public interface.
