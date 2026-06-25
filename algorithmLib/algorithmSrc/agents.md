# Algorithm Library Naming Notes

Use `v` for variables and `a` for arrays.

## Standard Name Format

`v{variable_count}a{array_count}_{purpose}`

- `vN` means the standard layout exposes `N` variable containers named `v1` through `vN`.
- `aN` means the standard layout exposes `N` array containers named `a1` through `aN`.
- The suffix after the standard name describes what the algorithm bundle does.

## Standard Container Alias

- 标准容器的底层架构保持不变，运行时只认 `vN` 和 `aN`。
- 开发者在声明完 `vXaY` 之后，可以在 `container` 里继续声明这些标准槽位的高层别名，供人和 agent 读取。
- 推荐写法是：
  - `"aliases": ["a1:vertex", "v1,v2:pos"]`
- `a1:vertex` 表示 `a1` 的可读别名是 `vertex`。
- `v1,v2:pos` 表示 `v1` 和 `v2` 这一组标准变量共同组成一个高级变量 `pos`，按特殊变量规则理解。
- 如果一个别名只映射到单个 `v` 或单个 `a`，它仍然分别遵循变量/数组自己的规则。
- 这些别名是算法侧的声明信息，不改变底层容器布局。
- agent 或开发工具可以直接使用这些高级名字进行设计、沟通和生成。
- 一旦要写入当前主干可执行的 package 内容，必须把这些别名全部还原回 `vN/aN`，不要把别名直接写进依赖运行时解析的字段里。

## Example

- `temporary_test_line_motion`
  - `v1`, `v2`, `v3`: initial point position
  - `a1`: moving point buffer

## Bundle Rules

- Keep the folder name, manifest name, and catalog entry name identical.
- Keep the prefix counts exact.
- Put the behavior description after the `vNxM` standard name.
- Prefer short, explicit bundle names that make the purpose obvious.

## devTask

- 状态：in_progress
- 最后更新：2026-06-22
- 当前进度：
  - 已完成：流水线按“一个 pipeline 作为一个算法单元”挂载与调度，目标表述统一为“增加吞吐”，不再使用“加速单次执行”。
  - 已完成：runtime transfer map 已收紧为单向线性 `stage -> nextStage`，禁止 fan-out / fan-in。
  - 已完成：原 `runtimeDecomposer/runtimeReflector` 的职责已经统一收口到 `PipelineStageBridge`。
  - 已完成：GPU 侧继续把隐式 stageBuffer 绑定在 standard container 共享 `a` 前缀里；CPU 侧改为同一 lane 的 bridge 共享一份 `interStageBuffer`，不再依赖标准容器里的隐式 stageBuffer 作为真正运行时缓冲。
  - 已完成：pipeline stall 会输出每个 stage 的耗时和原因，并在后端导出 `csv` / `mermaid` 时序文件到 `artifacts/pipeline_timing/`。
  - 已完成：stage 支持声明 `runtime.pipeline.externalWriteResetContainers`，表示这些容器必须每帧从外部重写，stage 执行完成后立即清空。
  - 已跳过：最初的“环形探针”方案已废弃，后续以 stall 日志和 fail-fast 为主。

- [x] 0. 流水线本质是一组算法被当成一个算法提交，由 agent 来梳理它们的流水关系，目标是增加吞吐，不是加速单次执行。
- [x] 1. 流水线需要映射表，标准容器改写成映射表，并随算法本身提交到 runtime sys 这一层。
- [x] 2. 映射表只保留线性 `stage->nextStage` 关系，不再支持一个 stage 映射到多个 stage，也不再支持一个 stage 接收多个 stage 的映射。
- [skip] 3. 原始“环形探针”方案已废弃；按后续决策不再实现。
- [x] 4. 原 `runtimeDecomposer/runtimeReflector` 方案已由统一的 `PipelineStageBridge` 取代。
- [x] 5. debugTool 后端会保留 pipeline 总耗时和各 stage 耗时，并在 stall 时导出 `csv` / `mermaid` 图文件。
- [x] 6. 已增加 `runtime.pipeline.externalWriteResetContainers`，用于声明每帧外部重写、stage 执行后立即清空的容器。
- [x] 7. 如果流水线算法持续 tick 不动，直接输出每个 stage 的耗时和原因日志，然后断言报错；真正“弹出算法”的动作仍留待后续。

## Pipeline Mapping Notes

- pipeline 的 runtime transfer map 现在不只描述 `stage -> nextStage` 的边。
- 它还要描述每个 stage 的标准容器布局摘要：
  - `declared_variable_count`
  - `declared_array_count`
  - `shared_variable_count`
  - `shared_array_count`
  - `extra_variable_count`
  - `extra_array_count`
  - `extra_variable_offset`
  - `extra_array_offset`
- 含义是：
  - 所有 stage 共享一段结构兼容的 `v/a` 前缀
  - 某个 stage 如果额外多声明了几个 `v` 或 `a`，这些额外槽不会混进共享前缀
  - 它们会按照 stage 顺序累计偏移，再登记到映射表里
- 当前主干先收紧成只允许额外 `v`。
- 如果某个 stage 额外多出了共享前缀之外的 `a`，运行时直接断言报错。
- 例子：
  - stage0 额外多 1 个 `v`，偏移是 `0`
  - stage1 额外多 2 个 `v`，偏移是 `1`
  - stage3 额外多 1 个 `v`，偏移是 `3`
- GPU pipeline 强制要求隐式 stageBuffer 落在所有 stage 共享的 `a` 前缀里，不能挂到某个 stage 私有多出来的 `a` 上。
- CPU pipeline 的 bridge 运行时不再把这块隐式 stageBuffer 当成真正的跨 stage 缓冲；CPU 会为每条 lane 维护一份共享 `interStageBuffer`，所有 stage bridge 共同读写这份缓冲，并按映射表偏移解释额外 `v`。
- 如果某个 stage 的某些容器必须每帧都从外部重写，可以在 package 的 `runtime.pipeline.externalWriteResetContainers` 里列出它们；该 stage 执行完成后，这些容器会被立即清零，避免旧值在流水线里滞留。

## Package Format

Use one unified package file per algorithm bundle:

- `<algorithm_name>_package.json`
- The file contains `container`, `decomposer`, `reflector`, and `intervention` sections.
- The same package file can be used by the host, SDK, and debug tool.
- GPU tick shaders receive viewport width/height push constants and interpret algorithm-space positions as lower-left origin pixel coordinates before converting to clip space.
- `container` only describes storage layout such as count, shape, and scalar bit width. It does not declare whether payload bytes are `int`, `float`, or packed bits.
- Descriptor encoding belongs to `decomposer.description`, and reflection decoding belongs to `reflector`.
- If a `decomposer.description` item omits `codec`, runtime currently falls back to `float` for compatibility. Do not teach that `32` or `64` alone implies a semantic scalar type.

Use `cjson` style comments in the package file examples:

- Any text after `//` is commentary for the agent and developer.
- `//` comments are not part of the runtime data model.
- Runtime parsers should ignore `//` comments before parsing the JSON body.

### Example

```cjson
{
  "algorithm_name": "temporary_test_line_motion", // bundle name
  "globalCfg": {
    "solvePrecision": "32", // global scalar bit width for the package
    "defaultPrecision": "32"
  },
  "container": {
    "variable": 3, // create v1..v3
    "variableArray": 1, // create a1
    "aliases": [
      "v1,v2,v3:initial_point",
      "a1:point_buffer"
    ]
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
        // optional: add "codec": "float" / "int" / "uint" when the boundary must be explicit
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
      "resultRender": {
        "stage_name": "resultRender",
        "stage_kind": "resultRender",
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
- `resultRender` is the default place to attach GPU-side render work before the actual preview render pass.
