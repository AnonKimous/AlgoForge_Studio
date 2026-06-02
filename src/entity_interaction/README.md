# Entity Interaction Backend

This layer owns the runtime backend for assembling entities.

## What the UI Does

- Load a mesh file into the active runtime mesh.
- Create a new entity manually from the active mesh.
- Bind the entity to a render role, a physics role, or a shared render+physics role.
- Bind the entity to a known algorithm preset.
- Set the mounted agent name and load-mesh name for entity-level presets before creating them.
- Reset and reload entity bindings when the mesh source changes.
- Loading a new mesh clears existing entity bindings so the draft stays aligned with the new vertex layout.

## Notes

- The backend is intentionally direct and entity-centric.
- The editor UI sits above this layer and owns the rendering of entity controls.
- When an intervention package is mounted, it owns the UI semantics, signal protocol, and intervention delegation for that entity.
- Algorithm names still need to match the implementation contract.
- GPU physics presets require a compiled shader path in the editor UI create panel.
