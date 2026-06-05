# Interact UI Layer

This layer owns the editor-facing UI that sits above `agent_management`.
It is the app-facing entry point for the editor surface and delegates agent work downward.

## Responsibilities

- Render editor-facing inspection panels.
- Read agent runtime state from `agent_management`.
- Present intervention and signal state to the user in an editor-friendly form.
- Own the facade that binds UI actions to `agent_management` and `runtime_systems::RuntimeEnvironment`.

## Notes

- The layer is UI-only and should not own algorithm execution.
- `agent_management` stays responsible for the runtime agent registry and macro tick backend.
- Intervention packages may contribute custom UI through the agent-owned intervention hook, but the facade stays here.
- This layer should not reach into `SdlWindow` or `ImGuiVulkanRuntime` directly.
