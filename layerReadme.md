# Layer Readme

## Core Rules

- Lower layers do not depend upward.
- Same-layer modules do not directly depend on each other.
- Cross-cutting coordination is lifted to upper orchestration layers.

## Active Layer Map

1. `common_data`
2. `runtime_systems`
3. `messaging`
4. `algorithm_library`
5. `algorithm`
6. `codec`
7. `orchestration_entity`
8. `agents`
9. `instance_interaction`
10. `app_orchestration`

## Important Notes

- `foundation` and `data_protocol` concepts are removed and merged into `common_data`.
- `physmng`, `PhysModeController`, decomposition legacy components, pick/snapshot legacy channels, and guide-ui relay legacy components are removed from active code.
- Agent-side direct algorithm parameter edits must go through codec intervention tools.
- `algorithm` is a manager layer only.
- Ordinary algorithm packages live under `algorithm_library`.
- Orchestration entities live under `orchestration_entity`.
- `algorithm_library/camera` is the ordinary camera package that can be held by orchestration entities.

## Dependency Direction

Recommended direction:

`common_data -> runtime_systems -> messaging -> algorithm_library -> algorithm -> codec -> orchestration_entity -> agents -> instance_interaction -> app_orchestration`
