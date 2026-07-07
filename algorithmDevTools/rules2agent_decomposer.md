# rules2agent_decomposer

请把下面这组规则同步进清单对应的 `agents.md`，保持语义一致，不要改坏已有的 `containerElement` 规则。

## Decomposer 语义

1. `decomposer` 是一个特殊工具框，内部上下分成两个窗口。
2. 上侧窗口是 `descriptor`，下侧窗口是 `resource`。
3. 容器到拆解器表示资源反射，拆解器到容器表示资源拆解。
4. `descriptor` 支持两类拆解方式：
   - 直接映射：`a2a`、`v2v`、`v2a`
   - 过滤映射：统一叫 `filter`，本质是一个函数脚本
5. `resource` 也支持两类拆解方式：
   - 默认拆解：右键拆解即可
   - 过滤拆解：统一叫 `filter`，需要特殊函数脚本
6. 最终输出仍然是变量/数组一类的结果，但用户可以把它们接到容器、`containerElement`，或者自定义合并名字上。
7. 如果连接两边都使用了自定义数据类型，必须要求结构严格对应。
8. 如果结构对不上，必须红色高亮并禁止输出，不允许静默失败。

## 结构规则

1. `containerElement` 依然是可拖动、可缩放的框。
2. `containerElement` 内部只允许看到并选择自己包裹的变量、数组和子容器。
3. `containerElement` 的右键详情需要递归展开，保持括号树风格。
4. 容器/分组拖动时，内部成员必须跟着一起移动。
5. 画布缩小时，内部成员可以被裁切，但不能被强行拖出容器边界。

## 清单规则

1. `decomposer.res` 需要作为资源结构清单保留。
2. `decomposer.description` 需要保留 `mapKind`、`script`、`resourceMode`、`resourceScript`。
3. `filter` 映射和 `filter` 拆解要统一用这个名字，不要再拆成两个不同术语。
4. 如果清单里出现自定义结构，优先按树结构解释，而不是按单个变量/数组解释。

## Pipeline Bridge Note

1. `standard container` is the preferred pipeline path.
2. Pipeline and normal algorithms are submitted through separate backend paths.
3. `PipelineStageBridge` should resolve `standard_layout` first and prefer standard-slot interpretation.
4. Non-standard containers are not a soft fallback: they must be transferred as same-name containers with identical structure, or the runtime should fail fast.
5. Standard slots like `v1`, `v2`, `v3` may be grouped into more readable logical names, but runtime should still resolve the underlying standard slots.
6. Tool-side alias declarations such as `a1:vertex` and `v1,v2:pos` are allowed for authoring, display, and planning.
7. When the tool emits package fields that the current trunk runtime will parse, it must write the underlying `vN/aN` names instead of alias names.
8. Direct transfer for non-standard containers must keep the same container name on both sides and the same structure.
9. If an algorithm depends heavily on non-standard containers, the toolchain should warn the developer to move back toward standard containers.
