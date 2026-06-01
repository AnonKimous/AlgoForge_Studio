# Layer Readme

## Core Rules

- Lower layers do not depend upward.
- Same-layer modules do not directly depend on each other.
- Cross-cutting coordination is lifted to upper orchestration layers.
- `orchestration_entity` is a cross-layer carrier, not a pure leaf layer.
- `agents` stay below `entity_interaction`; the dependency is one-way.

## Active Layer Map

1. `common_data`
2. `runtime_systems`
3. `messaging`
4. `algorithm_library`
5. `algorithm`
6. `codec`
7. `orchestration_entity`
8. `agents`
9. `entity_interaction`
10. `app_orchestration`

## Important Notes

- `foundation` and `data_protocol` concepts are removed and merged into `common_data`.
- `physmng`, `PhysModeController`, decomposition legacy components, pick/snapshot legacy channels, and guide-ui relay legacy components are removed from active code.
- Agent-side direct algorithm parameter edits must go through codec intervention tools.
- `algorithm` is a manager layer only.
- Ordinary algorithm packages live under `algorithm_library`.
- Orchestration entities live under `orchestration_entity` and may carry ordered cross-layer package/descriptor data plus pipeline routing metadata.
- `algorithm_library/camera` is the ordinary camera package that can be held by orchestration entities.
- `entity_interaction` composes `agents` and owns the app-level runtime loop.
- `entity_interaction` also exposes the editor UI for loading meshes and creating instances by hand.
- That UI can also create paired render/physics instances for the random vertex motion preset.
- `agents` do not depend on `entity_interaction`.
- A single orchestration entity can bundle multiple algorithm packages to represent one render or simulation pipeline, and the entity owns the package order plus container routing.
- The final composite compliance descriptor is where package-scoped container aliases live.

## Dependency Direction

Recommended direction:

`common_data -> runtime_systems -> messaging -> algorithm_library -> algorithm -> codec -> orchestration_entity -> agents -> entity_interaction -> app_orchestration`
