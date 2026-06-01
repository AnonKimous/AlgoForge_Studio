# Entity Interaction Layer

This layer owns the runtime editor UI for assembling entities.

## What the UI Does

- Load a mesh file into the active runtime mesh.
- Create a new entity manually from the active mesh.
- Bind the entity to a render role, a physics role, or a shared render+physics role.
- Bind the entity to a known algorithm preset.
- Create a shared random-vertex-motion entity that mounts both render and physics roles.
- Reset and reload entity bindings when the mesh source changes.
- Loading a new mesh clears existing entity bindings so the draft stays aligned with the new vertex layout.

## Notes

- The UI is intentionally direct and entity-centric.
- Algorithm names still need to match the implementation contract.
- GPU physics presets require a compiled shader path in the create panel.
- The random vertex motion preset uses the draft radius as the default motion radius and routes the result through the existing pause/reset intervention flow.
