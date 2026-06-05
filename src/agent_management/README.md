# Agent Management

This layer owns the runtime manager for agents.

## Responsibilities

- Create agents from upper-layer creation specs.
- Keep all created agents inside one manager instead of rebinding a single active agent.
- Drive one macro ticker per managed agent every frame.
- Stay below `interact_ui` and above `agent`.

## Notes

- Upper layers hand finished creation specs down here.
- This layer should not reach into SDL, window ownership, or ImGui/Vulkan setup.
- `AgentManager` is the public surface of the layer.

