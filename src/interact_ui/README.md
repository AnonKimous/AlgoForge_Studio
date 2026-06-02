# Interact UI Layer

This layer owns the editor-facing UI that sits above `agent_execute`.
It is the app-facing entry point for the editor surface and delegates agent work downward.

## Responsibilities

- Render the manual agent creation and inspection panels.
- Read agent runtime state from `agent_execute`.
- Present intervention and signal state to the user in an editor-friendly form.
- Delegate agent creation and binding actions back down to `agent_execute`.

## Notes

- The layer is UI-only and should not own algorithm execution.
- `agent_execute` stays responsible for the agent runtime backend, agent composition, and mesh-backed agent slots.
- Intervention packages may still contribute UI semantics through their signal and codec hooks, but the actual editor rendering lives here.
