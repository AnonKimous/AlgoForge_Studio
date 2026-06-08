# Interact UI Layer

This layer is split into two thin halves:

- `InteractUiRuntime` owns the interaction host surface, runtime shell, and frame timing.
- `InteractUiPanel` owns the editor-facing UI and depends only on the interaction host interface.

## Responsibilities

- `InteractUiRuntime` owns window/runtime lifetime, the managed agent registry, and the data exposed to UI.
- `InteractUiRuntime` does not own the panel.
- `InteractUiPanel` renders editor-facing inspection and creation panels.
- `InteractUiPanel` reads agent runtime state through `IInteractUiHost`.
- The panel only previews algorithm-side state and does not keep a mesh copy alive.
- Intervention packages may contribute custom UI through the agent-owned intervention hook, but the panel stays here.

## Notes

- The UI is intentionally thin and stays separate from interaction state.
- `app_orchestration/main.cpp` composes `InteractUiRuntime` and `InteractUiPanel`.
- `agent_management` stays responsible for the runtime agent registry and macro tick backend.
- `InteractUiPanel` should not reach into `SdlWindow` or `ImGuiVulkanRuntime` directly.
