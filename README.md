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
- `codec` converts resource + descriptor data into algorithm-compliance payloads and keeps the algorithm intervention channel.
- `algorithm` executes CPU/GPU physics and exposes compliance contracts.
- `common_data` is the unified shared data layer.

## Build

Use the project script:

```bat
build_msvc.cmd build
```

If needed, run configure + build:

```bat
build_msvc.cmd
```
