# Agent Capability

This capability module owns the lightweight `Agent` data object.

## What It Keeps

- Agent name and a list of attached algorithms.
- A list of algorithm codec groups attached to this agent.
- Each algorithm codec group directly holds its own profile, reflector, decomposer, and intervention objects.

## Package Hook Split

- The reflector is for encode/decode and selective key-data reflection only.
- When a package uses manifest-backed reflection, the reflector should come from
  the algorithm-management manifest instead of ad-hoc descriptor state.
- The decomposer is responsible for resource reflection and initialization-level decomposition data.
- Intervention objects are held alongside the matching algorithm entry instead of through a separate package wrapper.

## What It Does Not Do

- It does not own outer runtime scheduling.
- It does not own SDL / ImGui / Vulkan frame orchestration.
- It does not own render/physics variants.
- It does not model a pipeline graph or route metadata.
- It does not cache resource contracts or container-slot descriptions internally.
