# min_vulkan_win32

This project is a layered Vulkan + SDL3 physics/render sandbox.

## Current Runtime Controls

The UI keeps only these simulation controls:

- Run simulation
- Pause simulation
- Step one frame
- Reset simulation
- Animation page

## Current High-Level Flow

- `agents` own lifecycle and host-facing API wrappers.
- `agents` can also own shared algorithm pools, intervention requests, and agent<->algorithm signals.
- `codec` converts resource + descriptor data into algorithm-compliance payloads and keeps the intervention packet codec.
- `algorithm` is now the manager layer only.
- `algorithm_library` hosts ordinary algorithm packages and ordinary execution contracts, including the camera package.
- `orchestration_entity` hosts the organization entity that ties multi-layer work together.
- `common_data` is the unified shared data layer.
- `instance_interaction` is the instance interaction layer and depends on `agents` for simulation control.

## Build

Use the project script:

```bat
build_msvc.cmd build
```

If needed, run configure + build:

```bat
build_msvc.cmd
```
