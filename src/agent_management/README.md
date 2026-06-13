# Agent Management

This layer owns the runtime manager for agents.

## Responsibilities

- Create agents from upper-layer creation specs.
- Mount and unmount algorithms on managed agents.
- Keep all created agents inside one manager instead of rebinding a single active agent.
- Drive one macro ticker per managed agent every frame.
- Forward descriptor and resource data down to the agent when mounting algorithms.
- Check each managed agent's tick budget before ticking it.
- Stay below `debug_tool` and `sdk`, and above `agent`.

## Notes

- Upper layers hand finished creation specs down here.
- This layer should not create containers itself.
- This layer should not reach into SDL, window ownership, or ImGui/Vulkan setup.
- `AgentManager` is the public surface of the layer.
