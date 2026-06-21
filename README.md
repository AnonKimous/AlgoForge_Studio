# physBased-render

## 中文

`small-algorithm-kernel` 是一个面向多阶段算法流水线执行、验证和可视化的分层内核，同时提供 AI agent 辅助的低代码算法开发环境。

它的核心不是单个算法，而是一条可以被拆分、组装、执行和调试的算法流水线：

- 从资源描述出发，将 mesh、描述符和其他输入拆解成算法可消费的标准形式
- 用 `manifest + CPU cpp + GPU shader + container` 描述一个可执行算法包
- 把一个算法拆成多个 stage，并按流水线方式串联起来
- 支持 CPU 和 GPU 两条执行路径
- 支持结果反射回传、规则检查和异常信号上报
- 支持介入器干预，便于调试、验证和回放
- 支持 `debugTool` 挂载调试，直接查看资源、运行状态和反射结果
- 支持流水线资源流转、阶段衔接和运行时状态追踪

### 流水线能力

- 一个算法可以按多个 stage 组织成流水线
- 每个 stage 都有明确的输入、输出和资源绑定
- 流水线可以按顺序执行，也可以按约定进入循环式提交路径
- 流水线运行时可以回传中间状态、最终结果和异常信号
- debugTool 可以直接观察流水线当前跑到哪一段、哪一个 stage 卡住、哪一段数据没有推进
- 介入器可以参与流水线的检查和修正，而不是只看最终结果

### 项目能做什么

- 构建和运行算法包
- 管理容器、资源绑定和执行状态
- 在 CPU 和 GPU 两侧执行同一套算法逻辑
- 把算法结果反射回宿主环境
- 通过 debugTool 观察、调试和验证运行过程
- 通过 AI agent 接入算法工具链，辅助结构组织、资源检查和文本生成

### 项目的优点

- 协议统一，算法定义、资源拆解、容器组织和执行结果都遵循同一套约定
- 数据模型极简，底层只保留 `v` 和 `a` 这类标准单元，减少多余封装
- 工具链闭环，从算法生成、批量构建、挂载、执行到结果反射都能串起来
- CPU / GPU 双路径并行支持，便于对比验证和能力扩展
- AI agent 可以进入同一条生产链路，承担结构组织、检查和辅助输出任务

### 项目怎么组织

- `common_data`：共享数据、数学、输入和交互类型
- `runtime_systems`：窗口、ImGui 和 Vulkan 运行时支持
- `algorithm_management`：算法包加载、容器创建、插件装配和统一入口
- `agent`：运行时 agent 对象，负责挂载和提交
- `agent_management`：agent 创建编排、挂载/卸载、描述符转发和 tick
- `debugTool`：交互式调试入口
- `sdk`：外部开发者提交算法和 agent 的入口
- `algorithmLib/algorithmSrc`：算法包源码镜像
- `algorithmLib/algorithmruntimeLib`：构建后的运行时产物

### 工作流

1. 通过工具或编辑器准备算法资源和描述信息
2. 按约定组织成算法包
3. 构建生成 CPU / GPU 对应产物
4. 挂载到 `debugTool` 或 SDK 入口
5. 执行后查看反射结果、异常信号和调试信息
6. 需要时通过介入器和 AI agent 继续检查与辅助修正

### Demo

- `v6a6_pbd_ball_collision_demo`：用于验证刚体球碰撞、BVH 检查、连续碰撞检测、反射与介入链路
- `temporary_test_line_motion`：用于验证基础的 GPU / 数据流执行路径

### 约定

- 项目使用左手坐标系
- 原点 `[0,0,0]` 位于左下近角
- `+X` 向右，`+Y` 向上，`+Z` 指向屏幕内
- Render Preview 中的 `x` 和 `y` 表示预览页像素坐标
- `实例` 统一按 `agent` 理解

## English

`small-algorithm-kernel` is a layered kernel for multi-stage algorithm pipeline execution, validation, and visualization, with an additional low-code development environment for AI agents.

Its core is not a single algorithm, but a pipeline that can be split, assembled, executed, and debugged:

- Start from resource descriptions and split mesh, descriptors, and other inputs into algorithm-ready forms
- Describe an executable package with `manifest + CPU cpp + GPU shader + container`
- Break one algorithm into multiple stages and connect them as a pipeline
- Support both CPU and GPU execution paths
- Reflect execution results back to the host, with rule checks and error reporting
- Allow intervention hooks for debugging, validation, and replay
- Attach packages to `debugTool` for direct inspection of resources, runtime state, and reflection data
- Track pipeline resource flow, stage handoff, and runtime state
- Expose forced sync and non-forced sync modes, with timing statistics only in forced sync

### Pipeline Capabilities

- An algorithm can be organized into multiple stages as a pipeline
- Each stage has explicit inputs, outputs, and resource bindings
- The pipeline can run sequentially or enter a circular submission path by convention
- The runtime can report intermediate state, final results, and error signals
- `debugTool` can show which stage is active, which stage is stalled, and which data has not advanced
- Intervention hooks can inspect and correct the pipeline, not just the final output

### What it can do

- Build and run algorithm packages
- Manage containers, resource bindings, and execution state
- Execute the same algorithm logic on both CPU and GPU
- Reflect algorithm output back into the host environment
- Inspect, debug, and validate runtime behavior through `debugTool`
- Bring AI agents into the same toolchain for structure organization, resource checks, and text generation

### Why it stands out

- Unified protocol across algorithm definition, resource decomposition, container layout, and result reflection
- Minimal data model, with `v` and `a` as the base units to reduce unnecessary abstraction
- End-to-end tooling from generation and batch build to mounting, execution, and reflection
- Native support for both CPU and GPU execution paths
- AI agents can participate in the same production flow for structure, checking, and assisted output

### Architecture

- `common_data`: shared data, math, input, and interaction types
- `runtime_systems`: windowing, ImGui, and Vulkan runtime support
- `algorithm_management`: package loading, container creation, plugin assembly, and unified entrypoints
- `agent`: runtime agent objects that handle mounting and submission
- `agent_management`: agent orchestration, mount/unmount, descriptor forwarding, and ticking
- `debugTool`: interactive debugging entrypoint
- `sdk`: external entrypoint for submitting algorithms and agents
- `algorithmLib/algorithmSrc`: mirrored algorithm source packages
- `algorithmLib/algorithmruntimeLib`: built runtime artifacts

### Workflow

1. Prepare algorithm resources and descriptions through tools or the editor
2. Package them according to the project conventions
3. Build CPU / GPU artifacts
4. Mount the results into `debugTool` or submit through the SDK
5. Inspect reflection data, error signals, and debugging information after execution
6. Use intervention hooks and AI agents to continue checking and refinement when needed

### Demos

- `v6a6_pbd_ball_collision_demo`: validates rigid ball collision, BVH checks, continuous collision detection, reflection, and intervention flow
- `temporary_test_line_motion`: validates the basic GPU/data flow execution path

### Conventions

- The project uses a left-handed coordinate system
- The origin `[0,0,0]` is at the lower-left near corner
- `+X` points to the right, `+Y` points upward, and `+Z` points into the screen
- In Render Preview, `x` and `y` refer to preview-page pixel coordinates
- `实例` is treated as `agent`
