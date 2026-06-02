# Layer Readme

## Core Rules

- Lower layers do not depend upward.
- Same-layer modules do not directly depend on each other.
- Cross-cutting coordination is lifted to upper orchestration layers.
- The agent module is a cross-boundary carrier, not a pure leaf layer.
- `agent_execute` owns the runtime agent backend.
- `interact_ui` stays above `agent_execute`; the dependency is one-way.

## Active Layer Map

1. `common_data`
2. `runtime_systems`
3. `messaging`
4. `algorithm_library`
5. `algorithm`
6. `codec`
7. `agent`
8. `agent_execute`
9. `interact_ui`
10. `app_orchestration`

## Important Notes

- `foundation` and `data_protocol` concepts are removed and merged into `common_data`.
- `physmng`, `PhysModeController`, decomposition legacy components, pick/snapshot legacy channels, and guide-ui relay legacy components are removed from active code.
- Agent-side direct algorithm parameter edits must go through codec intervention tools.
- `algorithm` is a manager layer only.
- Ordinary algorithm packages live under `algorithm_library`.
- Agents may carry ordered cross-layer package/descriptor data plus pipeline routing metadata.
- `algorithm_library/camera` is the ordinary camera package that can be held by orchestration agents.
- `agent_execute` composes the agent backend and owns the app-level runtime backend.
- `interact_ui` sits above `agent_execute` and exposes the editor UI for loading meshes and creating agents by hand.
- `agent_execute` does not depend on `interact_ui`.
- A single orchestration agent can bundle multiple algorithm packages to represent one render or simulation pipeline, and the agent owns the package order plus container routing.
- The final composite compliance descriptor is where package-scoped container aliases live.

## Dependency Direction

Recommended direction:

`common_data -> runtime_systems -> messaging -> algorithm_library -> algorithm -> codec -> agent -> agent_execute -> interact_ui -> app_orchestration`
