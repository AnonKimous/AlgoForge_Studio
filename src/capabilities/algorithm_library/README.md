# Algorithm Library Capability Bundles

This directory stores concrete algorithm bundle assets.

The directory is built by the dedicated batch tool `check_sdk.bat`, so it can be compiled independently from the `debugTool` solution. The packaged mirror is written to `algorithmLib/algorithmSrc`, and runtime DLL/SPV output is written to `algorithmLib/algorithmruntimeLib`.

## Layout

Each algorithm lives in its own subdirectory named after the algorithm:

```text
algorithmLib/algorithmSrc/<algorithm_name>/
algorithmLib/algorithmruntimeLib/<algorithm_name>/
```

Each algorithm directory may contain:

- `<algorithm_name>_package.json`
  - unified bundle manifest containing container, decomposer, reflector, and intervention sections
- `runtime.launchOnce`
  - optional boolean runtime hint
  - when `true`, the host runs the algorithm once and then keeps it out of later agent ticks
  - the first successful post-run reflection snapshot is cached and reused
- intervention stage names use `preTick` and `afterTick`; `resultRender` stays separate
- intervention stages are wrapper stages around the algorithm body, not the body itself
- `resultRender` stages may carry GPU `shader` entries together with array bindings
- GPU bundles may also attach the same render shader to `afterTick` so execution and preview share one submission
- `resultRender` may stay in the package for CPU-side render code
- `<algorithm_name>.dll`
  - optional algorithm plugin module exporting the bundle entrypoints

The unified package manifest is the preferred format. Legacy split container,
decomposer, and reflector files are no longer used for new bundles.
Intervention stage names use `preTick` and `afterTick`; `resultRender` stays
separate. Intervention control uses a reserved one-byte signal slot.

Reflector entries live inside the package manifest:

- `reflector.name`
  - human-readable group name
- `reflector.functionName`
  - shared reflector function label for the group
- `reflector.refreshMode`
  - optional reflection refresh policy
  - use `everyTick` for the current behavior
  - use `captureOnce` or `onceAfterCompletion` to cache the first successful snapshot
- `reflector.items[]`
  - each item may declare `input`/`output` with `varity`/`array`
  - if `reflectFun` is `direct`, each item may also declare flat `from` and `to`
  - `output.v.name` or `output.a.name` names the reflected object

The plugin module is also optional. If it exists, the host tries to load it
first.

## UI Catalog

The UI reads `algorithm_catalog.json` from `algorithmLib/algorithmSrc` to populate the algorithm selection dropdown. The catalog lists available algorithms and points to their bundle names.

## Compatibility

Use the subdirectory layout above for new bundles.
See `agents.md` in this directory for `v`/`a` naming rules.

## Coordinate Convention

- Algorithm shaders and preview math follow the repo-wide left-handed convention.
- Treat `[0,0,0]` as the lower-left near corner.
- `+X` is right, `+Y` is up, and `+Z` is into the screen.
- Do not assume Vulkan's default framebuffer orientation; the runtime flips it for you.
- GPU tick shaders receive viewport width/height push constants and interpret algorithm-space positions as lower-left origin pixel coordinates before converting to clip space.
- Result-render shaders consume preview-page pixel coordinates.
- In Render Preview, `[0,0]` maps to the lower-left corner of the content region.
- GPU-side render work is attached to `afterTick` by default and runs before the preview render pass.
