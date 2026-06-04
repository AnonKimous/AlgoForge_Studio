# Interact UI Layer

This layer owns the editor-facing UI that sits above `agent_execute`.
It is the app-facing entry point for the editor surface and delegates agent work downward.

## Responsibilities

- Render mesh loading and bound-agent inspection panels.
- Read agent runtime state from `agent_execute`.
- Present intervention and signal state to the user in an editor-friendly form.

## Notes

- The layer is UI-only and should not own algorithm execution.
- `agent_execute` stays responsible for the runtime binding backend and the bound agent only.
- Intervention packages may still contribute UI semantics through their signal and codec hooks, but the actual editor rendering lives here.
