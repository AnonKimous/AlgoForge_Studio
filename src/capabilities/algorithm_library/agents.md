# Algorithm Library Naming Notes

Use `v` for variables and `a` for arrays.

## Standard Name Format

`v{variable_count}a{array_count}_{purpose}`

- `vN` means the standard layout exposes `N` variable containers named `v1` through `vN`.
- `aN` means the standard layout exposes `N` array containers named `a1` through `aN`.
- The suffix after the standard name describes what the algorithm bundle does.

## Example

- `temporary_test_line_motion`
  - `v1`, `v2`, `v3`: initial point position
  - `a1`: moving point buffer

## Bundle Rules

- Keep the folder name, manifest name, and catalog entry name identical.
- Keep the prefix counts exact.
- Put the behavior description after the `vNxM` standard name.
- Prefer short, explicit bundle names that make the purpose obvious.

## Package Format

Use one unified package file per algorithm bundle:

- `<algorithm_name>_package.json`
- The file contains `container`, `decomposer`, `reflector`, and `intervention` sections.
- The same package file can be used by the host, SDK, and debug tool.
- GPU tick shaders receive viewport width/height push constants and interpret algorithm-space positions as lower-left origin pixel coordinates before converting to clip space.

Use `cjson` style comments in the package file examples:

- Any text after `//` is commentary for the agent and developer.
- `//` comments are not part of the runtime data model.
- Runtime parsers should ignore `//` comments before parsing the JSON body.

### Example

```cjson
{
  "algorithm_name": "temporary_test_line_motion", // bundle name
  "globalCfg": {
    "solvePrecision": "fp32", // global precision for the package
    "defaultPrecision": "fp32"
  },
  "container": {
    "variable": 3, // create v1..v3
    "variableArray": 1 // create a1
  },
  "decomposer": {
    "res": {
      "buffer": {}
    },
    "description": [
      {
        "name": "point_position",
        "from": ["start_x", "start_y", "start_z"],
        "to": ["v1", "v2", "v3"]
      }
    ]
  },
  "reflector": {
    "name": "positionABS",
    "functionName": "direct",
    "items": [
      {
        "name": "positionABS",
        "from": ["a1"],
        "to": ["positionABS"],
        "reflectFun": "direct"
      }
    ]
  },
  "intervention": {
    "stages": {
      "afterTick": {
        "stage_name": "afterTick",
        "stage_kind": "afterTick",
        "used_algorithm_containers": {
          "arrays": [
            {
              "name": "a1",
              "kind": "array",
              "tuple_width": 3,
              "required": true
            }
          ],
          "variables": []
        },
        "shader": {
          "pipeline": "graphics",
          "vertex": "temporary_test_line_motion_gpu_tick.vert",
          "fragment": "temporary_test_line_motion_gpu_tick.frag"
        }
      },
      "resultRender": {
        "stage_name": "resultRender",
        "stage_kind": "resultRender",
        "functions": [
          "ApplyResultRender"
        ],
        "shader": {
          "pipeline": "graphics",
          "vertex": "temporary_test_line_motion_result_render.vert",
          "fragment": "temporary_test_line_motion_result_render.frag"
        }
      }
    }
  }
}
```

## Coordinate Convention

- Use the repo-wide left-handed convention.
- Treat `[0,0,0]` as the lower-left near corner.
- `+X` points right, `+Y` points up, and `+Z` points into the screen.
- Render preview coordinates use the lower-left corner of the preview content region as `[0,0]`.
- `afterTick` is the default place to attach GPU-side render work before the actual preview render pass.
