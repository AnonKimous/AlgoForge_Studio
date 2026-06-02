# 2026-05-30 Notes

This note records the earlier runtime/UI cleanup pass.

## Main Changes

- Unified the runtime around `agent` terminology.
- Kept the creation UI default-filled so it is ready to create and run immediately.
- Switched rendering to the algorithm-side vertex array view instead of treating the source mesh as the render primitive.

## Runtime Split

- `runtime_systems` owns the backend runtime support.
- `agent_execute` owns the manual agent composer and runtime backend.
- `interact_ui` sits above the execution backend and renders the editor surface.
- The UI defaults to a shared agent preset and starts the work on creation.

## Validation

- The project builds successfully after the migration.
- Remaining build output is limited to the existing Eigen code-page warning and the unrelated post-build shell message.
