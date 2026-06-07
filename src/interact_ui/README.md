# Interact UI Layer

This layer is split into two thin halves:

- `InteractUiRuntime` owns the interaction host surface and the runtime shell.
- `InteractUiPanel` owns the editor-facing UI and depends only on the interaction host interface.

## Responsibilities

- `InteractUiRuntime` owns window/runtime lifetime and the managed agent registry.
- `InteractUiRuntime` does not own the mesh; resource paths are passed through as descriptors and validated during composition.
- `InteractUiPanel` renders editor-facing inspection and creation panels.
- `InteractUiPanel` reads agent runtime state through `IInteractUiHost`.
- The panel only previews algorithm-side state and does not keep a mesh copy alive.
- Intervention packages may contribute custom UI through the agent-owned intervention hook, but the panel stays here.

## Notes

- The UI is intentionally thin and stays separate from interaction state.
- `agent_management` stays responsible for the runtime agent registry and macro tick backend.
- `InteractUiPanel` should not reach into `SdlWindow` or `ImGuiVulkanRuntime` directly.
