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

## devTast

- 状态：in_progress
- 最后更新：2026-06-17
- 当前进度：runtime transfer 已收紧为单向线性链式映射，每个 stage 只能固定映射到下一个 stage，禁止分叉和汇聚；目标是增加吞吐，不是加速单次执行；卡死告警的 fail-fast 口子已留；映射表开始显式记录每个 stage 的共享标准槽前缀和额外标准槽偏移。

- [ ] 0. 流水线本质是一组算法被当成一个算法提交，由 agent 来梳理它们的流水关系，目标是增加吞吐，不是加速单次执行。
- [ ] 1. 流水线需要映射表，标准容器改写成映射表，并随算法本身提交到 runtime sys 这一层。
- [ ] 2. 映射表只保留线性 stage->nextStage 关系，不再支持一个 stage 映射到多个 stage，也不再支持一个 stage 接收多个 stage 的映射。
- [ ] 3. agent 里保留一个最简单的环形探针，debug 模式默认启动，发现环直接断言错误，release 不启动。
- [ ] 4. 参考 algorithmMNG 里的拆解器和反射器，新增 runtimeDecomposer 和 runtimeReflector，专门用于一次 tick 内的翻译、执行和输出，运行时反射负责抓映射表并写入对面的 runtimeDecomposer，运行时拆解负责解码传入的数据。
- [ ] 5. debugTool 后端增加一个功能，监视进入的算法从开始翻译数据到写出数据一共花了多久，并支持导出成图。
- [ ] 6. 增加一个标志，表示某些变量需要每帧都从外部写入，算法执行结束后，如果使用流水线算法，这些变量会被 runtimeReflector 擦除。
- [ ] 7. 如果流水线算法持续 tick 不动，先留一个 fail-fast 口子，直接输出日志包含每个 stage 耗时和原因，然后断言报错，真正弹出算法留到后续。

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
- pipeline 强制要求隐式 stageBuffer 落在所有 stage 共享的 `a` 前缀里，不能挂到某个 stage 私有多出来的 `a` 上。

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
