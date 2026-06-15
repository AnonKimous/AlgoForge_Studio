# Algorithm Library Naming Notes

Use `v` for variables and `a` for arrays.

## Standard Name Format

`v{variable_count}a{array_count}_{purpose}`

- `vN` means the standard layout exposes `N` variable containers named `v1` through `vN`.
- `aN` means the standard layout exposes `N` array containers named `a1` through `aN`.
- The suffix after the standard name describes what the algorithm bundle does.

## Example

- `simple_point_motion_demo`
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
  "algorithm_name": "simple_point_motion_demo", // bundle name
  "globalCfg": {
    "solvePrecision": "fp32", // global precision for the package
    "defaultPrecision": "fp32"
  },
  "container": {
    "variable": 6, // create v1..v6
    "variableArray": 2 // create a1..a2
  },
  "decomposer": {
    "res": {
      "mesh": {
        "vertex": "a1", // resource name -> container name
        "edge": "a2",
        "normal": "a3"
      }
    },
    "description": [
      // variable -> array
      {
        "name": "velocity", // human-readable only
        "from": ["v1", "v2", "v3"], // source variables
        "to": {
          "container": "a4", // target array container
          "indices": [1, 2, 3] // component positions
        }
      },
      // variable -> variable
      {
        "name": "control_copy",
        "from": ["v4"],
        "to": ["v5"]
      },
      // array -> array
      {
        "name": "mesh_copy",
        "from": ["a1"],
        "to": ["a2"]
      }
    ]
  },
  "reflector": {
    "name": "positionABS", // human-readable group name
    "functionName": "fun", // shared reflector function label
    "inputs": [
      "v1",
      "v2",
      "v3",
      "v4",
      "a1",
      "a2"
    ],
    "items": [
      // direct reflection item
      {
        "name": "positionABS",
        "from": ["v1", "v2", "v3"],
        "to": ["position_x", "position_y", "position_z"],
        "reflectFun": "direct"
      },
      // reflection item
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
      "preTick": {
        "stage_name": "preTick", // pre-execution stage
        "stage_kind": "preTick"
      },
      "afterTick": {
        "stage_name": "afterTick", // post-execution stage
        "stage_kind": "afterTick",
        "used_algorithm_containers": {
          "arrays": [
            {
              "name": "a1",
              "kind": "array",
              "tuple_width": 6,
              "required": true
            }
          ],
          "variables": []
        },
        "shader": {
          "pipeline": "graphics",
          "vertex": "simple_point_motion_demo_result_render.vert",
          "fragment": "simple_point_motion_demo_result_render.frag"
        }
      },
      "resultRender": {
        "stage_name": "resultRender", // preview stage
        "stage_kind": "resultRender",
        "functions": [
          "ApplyResultRender"
        ],
        "shader": {
          "pipeline": "graphics",
          "vertex": "simple_point_motion_demo_result_render.vert",
          "fragment": "simple_point_motion_demo_result_render.frag"
        }
      }
    },
    "controlSignal": {
      "byte": 0, // one-byte signal slot reserved for intervention
      "notes": "Any non-zero value means intervention is requested."
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
