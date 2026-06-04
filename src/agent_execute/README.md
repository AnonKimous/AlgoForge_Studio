# Agent Execute

This layer owns the agent runtime binding path.

## Responsibilities

- Keep the bound agent for runtime stepping.
- Drive `AgentTicker` every frame.
- Hold the bound `agent` directly inside `AgentTicker`.
- Let `AgentTicker` gate execution by agent signals.
- Stay below `interact_ui` and above `agent`.

## Notes

- Upper layers build launch specs and hand the finished binding request down here.
- Loading a new mesh clears the bound agent state.
- `AgentTicker` no longer owns physics session caches, fixed-step simulation state, or legacy algorithm request assembly.
- This layer should not reach into runtime shell internals such as SDL, window ownership, or ImGui/Vulkan setup.
