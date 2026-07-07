# Deferred Notes

Date: 2026-07-03

## Deferred items

- Vulkan VK dynamic layout expansion is deferred.
- The final decision is still open: rewrite, keep compatibility, or abandon.
- The Utah teapot PBR pipeline algorithm is deferred.

## Current decision

- Keep the old `va`-style fixed layout path for now.
- Revisit the VK layout model later as a separate refactor.
- Do not start the teapot algorithm implementation in this round.
