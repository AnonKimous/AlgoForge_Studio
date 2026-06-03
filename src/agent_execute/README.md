# Agent Execute

This layer owns the active-agent runtime path.

## Responsibilities

- Own the window/UI shell for the runtime.
- Keep the loaded agent slots and the current active agent binding.
- Drive `AgentTicker` every frame.
- Hold the current `agent` directly inside `AgentTicker` and forward execution to the algorithm manager.

## Notes

- The UI can create camera or physics agents from the current mesh.
- Loading a new mesh clears the current agent binding state.
- `AgentTicker` now lives here, not in `src/agents`.
