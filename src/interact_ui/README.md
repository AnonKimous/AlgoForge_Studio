# Interact UI Layer

This layer is split into two thin halves:

- `InteractUiRuntime` is the interaction host runtime backend and is compiled into `kernal_all`.
- `InteractUiPanel` owns the editor-facing UI and depends only on the interaction host interface.

## Responsibilities

- `InteractUiRuntime` owns window/runtime lifetime, the managed agent registry, and the data exposed to UI.
- `InteractUiRuntime` does not own the panel.
- `InteractUiPanel` renders editor-facing inspection and creation panels.
- `InteractUiPanel` reads agent runtime state through `IInteractUiHost`.
- The panel only previews algorithm-side state and does not keep a mesh copy alive.
- Intervention packages may contribute custom UI through the agent-owned intervention hook, but the panel stays here.
- Those intervention hooks are debug-only and are not exposed through the external SDK surface.

## Notes

- The UI is intentionally thin and stays separate from interaction state.
- `debugTool` composes `InteractUiRuntime` and `InteractUiPanel`.
- `kernal_all` stays responsible for runtime agent registry and macro tick backend.
- `InteractUiPanel` should not reach into `SdlWindow` or `ImGuiVulkanRuntime` directly.
