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

## devTast

- 状态：in_progress
- 最后更新：2026-06-17
- 当前进度：runtime transfer 已收紧为单向线性链式映射，每个 stage 只能固定映射到下一个 stage，禁止分叉和汇聚；目标是增加吞吐，不是加速单次执行；卡死告警的 fail-fast 口子已留。

- [ ] 0. 流水线本质是一组算法被当成一个算法提交，由 agent 来梳理它们的流水关系，目标是增加吞吐，不是加速单次执行。
- [ ] 1. 流水线需要映射表，标准容器改写成映射表，并随算法本身提交到 runtime sys 这一层。
- [ ] 2. 映射表只保留线性 stage->nextStage 关系，不再支持一个 stage 映射到多个 stage，也不再支持一个 stage 接收多个 stage 的映射。
- [ ] 3. 已移除环形探针，后续改用无进展时间的 stall 检测，debug 模式默认启动，触发后直接断言错误，release 也要直接返回失败。
- [ ] 4. 参考 algorithmMNG 里的拆解器和反射器，新增 runtimeDecomposer 和 runtimeReflector，专门用于一次 tick 内的翻译、执行和输出，运行时反射负责抓映射表并写入对面的 runtimeDecomposer，运行时拆解负责解码传入的数据。
- [ ] 5. debugTool 后端增加一个功能，监视进入的算法从开始翻译数据到写出数据一共花了多久，并支持导出成图。
- [ ] 6. 增加一个标志，表示某些变量需要每帧都从外部写入，算法执行结束后，如果使用流水线算法，这些变量会被 runtimeReflector 擦除。
- [ ] 7. 如果流水线算法长时间没有任何可观察进展，就直接输出日志，日志里包含每个 stage 的耗时和原因，然后断言报错；不要再按 tick 次数判断。

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

## Pipeline Bridge Rules

- Prefer `standard container` layouts for pipeline algorithms.
- Pipeline and non-pipeline algorithms are submitted through different backend paths; the backend owns the distinction.
- `PipelineStageBridge` should resolve `standard_layout` first and treat standard slots as the primary runtime carrier for stage-to-stage flow.
- Non-standard containers are not a soft fallback: they must be transferred as same-name containers with identical structure, or the runtime must fail fast.
- When moving non-standard containers, keep the developer-facing warning that they should not become a heavy pipeline dependency.
- Standard container slots such as `v1`, `v2`, `v3` may be aggregated into higher-level aliases for readability, but the runtime still resolves them through the underlying standard slots.
- Direct transfer for non-standard containers must use the same container name on both sides and identical structure; otherwise it is a runtime error.
- Pipeline stage order remains linear: `stage0 -> stage1 -> stage2 -> ...`.
