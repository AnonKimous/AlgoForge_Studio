# Layer Readme

## Core Rules

- Lower layers do not depend upward.
- Same-layer modules do not directly depend on each other.
- Cross-cutting coordination is lifted to upper orchestration layers.

## Active Layer Map

1. `common_data`
2. `runtime_systems`
3. `messaging`
4. `algorithm`
5. `codec`
6. `service_domains`
7. `interaction_analysis`
8. `agents`
9. `app_orchestration`

## Important Notes

- `foundation` and `data_protocol` concepts are removed and merged into `common_data`.
- `physmng`, `PhysModeController`, decomposition legacy components, pick/snapshot legacy channels, and guide-ui relay legacy components are removed from active code.
- Agent-side direct algorithm parameter edits must go through codec intervention tools.

## Dependency Direction

Recommended direction:

`common_data -> runtime_systems -> messaging -> algorithm -> codec -> service_domains -> interaction_analysis -> agents -> app_orchestration`
