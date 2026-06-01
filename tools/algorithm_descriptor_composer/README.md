# Algorithm Descriptor Composer

This helper reads algorithm compatibility descriptors, lets you order them into one instance-level pipeline, and assigns package-scoped container aliases.

## What It Produces

- `ordered_bindings`: the chosen execution order.
- `component_descriptors`: the original low-level descriptors.
- `composite_compliance_descriptor`: the final instance-level descriptor that can be handed to container generation code.

## JSON Input Shape

Each input file should look like this:

```json
{
  "package_name": "rotate_vertices",
  "algorithm_name": "rotate_vertices",
  "cpu_available": true,
  "gpu_available": false,
  "data_contract": {
    "arrays_to_allocate": [
      { "name": "vertexs", "element_count": 1, "element_stride": 12 }
    ],
    "temporary_registers_to_allocate": [],
    "temporary_caches_to_allocate": [],
    "filled_data_formats": [],
    "algorithm_required_formats": [],
    "container_aliases": []
  }
}
```

## Alias Rule

- Alias entries are package-scoped.
- The same source container name can be aliased differently in different packages.
- The tool does not validate whether the resulting graph is valid.
- If two entries collide after aliasing, the first one wins and the tool warns instead of rejecting the composition.

## Run

```bash
python tools/algorithm_descriptor_composer/descriptor_composer.py
```

On Windows, you can also run it with `py`:

```bat
py tools\algorithm_descriptor_composer\descriptor_composer.py
```
