# Editor UI Layer

This layer owns the editor-facing UI that sits above `entity_interaction`.
It is the app-facing entry point for the editor surface and delegates entity work downward.

## Responsibilities

- Render the manual entity creation and inspection panels.
- Read entity runtime state from `entity_interaction`.
- Present intervention and signal state to the user in an editor-friendly form.
- Delegate entity creation and binding actions back down to `entity_interaction`.

## Notes

- The layer is UI-only and should not own algorithm execution.
- `entity_interaction` stays responsible for the entity runtime backend, agent composition, and mesh-backed entity slots.
- Intervention packages may still contribute UI semantics through their signal and codec hooks, but the actual editor rendering lives here.
