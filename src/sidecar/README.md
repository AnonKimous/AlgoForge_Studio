# Sidecar Capability Modules

This directory hosts governed sidecar capability modules.

## Rules

- Sidecar modules stay outside the primary layer chain.
- They expose project-owned interfaces for optional capabilities such as file formats, persistence, and third-party adapters.
- They may depend on stable lower-layer types, but they must not be used to bypass the primary dependency order.
- Any business layer that uses a sidecar module must link it directly.
- One external capability should have one clear owner module.

## Current Modules

- `mesh_io`: OBJ mesh import/export built on top of `common_data::Mesh`.
