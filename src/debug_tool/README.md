# Debug Tool Split

This directory contains the debug host backend and the debug tool frontend.

## Responsibilities

- `DebugToolBackendRuntime` owns window/runtime lifetime, the managed agent registry, and the data exposed to the frontend.
- `DebugToolBackendRuntime` does not own the frontend panel.
- `DebugToolFrontendPanel` renders inspection, creation, preview, and custom intervention panels.
- `DebugToolFrontendPanel` reads only backend summary DTOs through `IDebugToolHost` and does not reach into `AgentManager` or `Agent` directly.
- The frontend only previews algorithm-side state and does not keep a mesh copy alive.
- Intervention packages may contribute custom frontend hooks through the agent-owned intervention hook, but the debug tool stays the integration point.
- Those intervention hooks are debug-only and are not exposed through the external SDK surface.

## Notes

- The frontend is intentionally thin and stays separate from runtime ownership.
- `debugTool` composes `DebugToolBackendRuntime` and `DebugToolFrontendPanel`.
- `debug_tool_backend` stays responsible for runtime agent registry, tick state, summary construction, preview request building, and other lower-level orchestration.
- `DebugToolFrontendPanel` should not reach into `SdlWindow` or `ImGuiVulkanRuntime` directly.
