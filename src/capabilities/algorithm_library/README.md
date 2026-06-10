# Algorithm Library Capability Bundles

This directory stores concrete algorithm bundle assets.

## Layout

Each algorithm lives in its own subdirectory named after the algorithm:

```text
src/capabilities/algorithm_library/<algorithm_name>/
```

Each algorithm directory may contain:

- `<algorithm_name>.json`
  - container manifest
- `<algorithm_name>_decomposer.json`
  - resource and descriptor requests used by the UI and decomposer
- `<algorithm_name>_reflector.json`
  - runtime reflector data
- `<algorithm_name>_intervention.json`
  - intervention hooks and metadata
- `<algorithm_name>.dll`
  - optional algorithm plugin module exporting the bundle entrypoints

The reflector and intervention files are optional. Some algorithms only provide
the container manifest and decomposer description.

The plugin module is also optional. If it exists, the host tries to load it
first.

## UI Catalog

The UI reads `algorithm_catalog.json` in this directory to populate the
algorithm selection dropdown. The catalog lists available algorithms and points
to their bundle names.

## Compatibility

Use the subdirectory layout above for new bundles.
