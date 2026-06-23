# AlgorithmScheduler Stage/Node 化改造冲突检查

## 1. 结论先行

基于当前 checkout，这次“`AlgorithmScheduler` 不再持有整条 pipeline，而是按 stage / 单算法分别持有，并把 stage 视为 node 放进哈希表”的方向，和现有实现存在三类硬冲突：

- 当前文档把 `AlgorithmScheduler` 明确定义为 pipeline runtime owner，而不是 node registry owner。
- 当前代码把 pipeline 当成一段连续的 stage 数组来挂载、查找、tick，不是松散 node 图。
- 当前 bridge / transfer 协议是 `stage_name -> stage_container_set` 的 stage 名字路由，不是 node id 路由。

如果直接改存储结构而不先改 owner、索引和协议，最后会出现一层说“按 node 管”，另一层还在按“连续 pipeline 片段”跑的撕裂状态。

## 2. 当前架构事实

### 2.1 文档层目前默认 `AlgorithmScheduler` 持有整条 pipeline

- [algorithm_scheduler_center.md](/D:/gptsandbox/algorithm_scheduler_center.md:41) 第 41-42 行写明“流水线算法的运行态不能散落在 `Agent` 里，调度中心要保存对应的运行时状态”。
- [algorithm_scheduler_center.md](/D:/gptsandbox/algorithm_scheduler_center.md:77) 第 77-84 行写明调度中心持有“流水线定义、lane 列表、stage 状态、owner 归属、运行统计”。
- [algorithm_scheduler_pipeline_model.md](/D:/gptsandbox/algorithm_scheduler_pipeline_model.md:12) 第 12 行写明“流水线的调度、推进、统计、挂载和卸载，都由 `AlgorithmScheduler` 统一管理”。
- [algorithm_scheduler_pipeline_model.md](/D:/gptsandbox/algorithm_scheduler_pipeline_model.md:124) 第 124-132 行继续把“持有流水线 runtime、维护提交和 tick 关系、维护同步统计”定义成 `AlgorithmScheduler` 职责。

### 2.2 最新 `pipelineDevDoc` 已收口为“算法语义不上 runtime_systems”

- [devtask.md](/D:/gptsandbox/devtask.md) 已明确要求：CPU pipeline 的算法语义、owner 和 runtime 状态留在 `agent` / `algorithm_management`，并补齐 stage 间临时自定义参数规则。
- [devtask.md](/D:/gptsandbox/devtask.md) 已明确说明 `runtime_systems` 不应理解 algorithm / pipeline / stage / lane / runtime transfer map。
- [devtask.md](/D:/gptsandbox/devtask.md) 已把分层重新收口为：`agent` 负责入口，`algorithm_management::AlgorithmScheduler` 负责 pipeline 注册与推进，`runtime_systems` 只保留下层执行原语。
- [devtask.md](/D:/gptsandbox/devtask.md) 已保留并统一了 stage 间临时自定义参数规则，把它们约束在 standard container / interStageBuffer / same-name custom container 这些通道里。

### 2.3 主干层级约束不允许随便跨层改 owner

- [AGENTS.md](/D:/gptsandbox/AGENTS.md:12) 第 12 行要求调用链保持 `sdk -> agent_management -> agent -> algorithm_management -> runtime_systems`。
- [src/README.md](/D:/gptsandbox/src/README.md:9) 第 9-12 行要求严格主干层只能依赖下一层公开接口。
- [src/README.md](/D:/gptsandbox/src/README.md:22) 第 22-23、42、45-46 行要求 `algorithm_management/algorithm_manager.h` 和 `runtime_systems/runtime_systems.h` 分别是各层唯一公开入口。

## 3. 当前代码事实

### 3.1 `AlgorithmScheduler` 现在就是“整条 pipeline runtime 持有者”

- [src/algorithm_management/algorithm_manager.h](/D:/gptsandbox/src/algorithm_management/algorithm_manager.h:62) 到 [src/algorithm_management/algorithm_manager.h](/D:/gptsandbox/src/algorithm_management/algorithm_manager.h:108) 暴露的是 `RegisterPipeline`、`RegisterPipelineRuntime`、`TryGetPipelineRuntime`、`UpdatePipelineRuntime` 这套“整条 pipeline”接口。
- [src/algorithm_management/algorithm_manager.h](/D:/gptsandbox/src/algorithm_management/algorithm_manager.h:116) 到 [src/algorithm_management/algorithm_manager.h](/D:/gptsandbox/src/algorithm_management/algorithm_manager.h:120) 的内部存储也是：
  `pipeline_registrations_`
  `pipeline_runtime_states_`
  `pipeline_runtime_ref_counts_`
- [src/algorithm_management/algorithm_abi.h](/D:/gptsandbox/src/algorithm_management/algorithm_abi.h:140) 到 [src/algorithm_management/algorithm_abi.h](/D:/gptsandbox/src/algorithm_management/algorithm_abi.h:192) 的 runtime 数据结构中心也是 `CpuPipelineRegistration`、`CpuPipelineLaneRuntimeState`、`CpuPipelineRuntimeState`，没有 node registry。

### 3.2 `Agent` 现在仍把 pipeline 当成“连续 stage 段”

- [src/agent/agent.cpp](/D:/gptsandbox/src/agent/agent.cpp:363) 到 [src/agent/agent.cpp](/D:/gptsandbox/src/agent/agent.cpp:392) 的 `_FindPipelineGroupRange` 通过 `pipeline_name + 连续 pipeline_stage_index` 来识别一整段 pipeline。
- [src/agent/agent.cpp](/D:/gptsandbox/src/agent/agent.cpp:1628) 到 [src/agent/agent.cpp](/D:/gptsandbox/src/agent/agent.cpp:1734) 在挂载时，把每个 mounted `AlgorithmObject` 写入 `pipeline_name`、`pipeline_stage_index`、`pipeline_stage_count`，然后整条 pipeline 的 runtime 一次性注册进 scheduler。
- [src/agent/agent.cpp](/D:/gptsandbox/src/agent/agent.cpp:1886) 到 [src/agent/agent.cpp](/D:/gptsandbox/src/agent/agent.cpp:2145) 的 tick 路径，先按 begin/end stage 区间取出一整组对象，再把它们作为一个 pipeline group 推进。

### 3.3 调度器内部 tick 仍按“整组 pipeline stage”执行

- [src/algorithm_management/algorithm_manager.h](/D:/gptsandbox/src/algorithm_management/algorithm_manager.h:1503) 到 [src/algorithm_management/algorithm_manager.h](/D:/gptsandbox/src/algorithm_management/algorithm_manager.h:1540) 的 `TickMountedPipeline` 入参本身就是 `mounted_objects + begin_index + end_index`。
- [src/algorithm_management/algorithm_manager.h](/D:/gptsandbox/src/algorithm_management/algorithm_manager.h:1701) 到 [src/algorithm_management/algorithm_manager.h](/D:/gptsandbox/src/algorithm_management/algorithm_manager.h:1724) 先构建整组 `stage_container_sets`，再计算可执行 stage 集合。
- [src/algorithm_management/algorithm_manager.h](/D:/gptsandbox/src/algorithm_management/algorithm_manager.h:1878) 到 [src/algorithm_management/algorithm_manager.h](/D:/gptsandbox/src/algorithm_management/algorithm_manager.h:2135) 的执行流程仍是“本轮所有可执行 stage 做 ingress -> execute -> egress”，最后一次性提交整组 stage 状态。

### 3.4 协议层还是 `stage_name -> container_set`，不是 `node_id -> runtime`

- [src/algorithm_support/algorithm_protocol.h](/D:/gptsandbox/src/algorithm_support/algorithm_protocol.h:81) 到 [src/algorithm_support/algorithm_protocol.h](/D:/gptsandbox/src/algorithm_support/algorithm_protocol.h:127) 的 ingress/egress/debug API 都接受 `target_stage_name` / `source_stage_name` 和 `unordered_map<string, shared_ptr<AlgorithmContainerSet>> stage_container_sets`。
- [src/algorithm_support/algorithm_package_decomposer.cpp](/D:/gptsandbox/src/algorithm_support/algorithm_package_decomposer.cpp:1270) 到 [src/algorithm_support/algorithm_package_decomposer.cpp](/D:/gptsandbox/src/algorithm_support/algorithm_package_decomposer.cpp:1276) 仍按 `target_stage_name` 查入边，并断言“不允许多个前驱”。
- [src/algorithm_support/algorithm_package_decomposer.cpp](/D:/gptsandbox/src/algorithm_support/algorithm_package_decomposer.cpp:1326) 到 [src/algorithm_support/algorithm_package_decomposer.cpp](/D:/gptsandbox/src/algorithm_support/algorithm_package_decomposer.cpp:1356) 仍按 `source_stage_name` 查出边，并通过 `stage_container_sets.find(outgoing_edge->target_stage_name)` 找下一 stage。

## 4. 与这次 Stage/Node 化方向的直接冲突

### 4.1 Owner 冲突

现在至少有两个 owner 说法：

- 文档 A：`AlgorithmScheduler` 持有整条 pipeline。
- 文档 B：pipeline 语义留在 `agent` / `algorithm_management`，`runtime_systems` 不理解 algorithm。
- 你的新方向：scheduler 不再持有整条 pipeline，而是分别持有 stage/node。

这里真正要定的是：在“算法语义不下沉到 `runtime_systems`”这个前提下，stage/node registry 最终落在 `agent` 兼容层，还是落在 `algorithm_management::AlgorithmScheduler`。

- 方案 A：`algorithm_management::AlgorithmScheduler` 持有 node registry，`Agent` 保留兼容视图。
- 方案 B：`Agent` 继续持有更多 pipeline/stage 视图，`AlgorithmScheduler` 只做部分注册/路由。

从现有分层和代码状态看，方案 A 更贴近 [devtask.md](/D:/gptsandbox/devtask.md) 和 [src/README.md](/D:/gptsandbox/src/README.md:22)，也更利于把 pipeline 语义从 `Agent` 继续收口到 `algorithm_management`。

### 4.2 数据模型冲突

当前 runtime 的核心状态是：

- pipeline 级：`CpuPipelineRuntimeState`
- lane 级：`CpuPipelineLaneRuntimeState`
- stage 级：`stage_has_data`、`stage_runtime_stats`

如果改成 node registry，至少要补一层明确模型：

- `PipelineDefinition`
- `StageNodeDefinition`
- `StageNodeRuntimeState`
- `LaneCursor` 或 `LaneToken`

否则只是把 `unordered_map` 换进去，逻辑依然还是“整条 pipeline 的数组状态”，那不是真正的 node 化。

### 4.3 `Agent` 视图冲突

`Agent` 当前默认假设：

- pipeline 在 `algorithm_objects_` 里是一段连续范围；
- range 是通过 `pipeline_stage_index` 顺序识别的；
- tick 时可以直接把 begin/end 范围交给 scheduler。

这和“node 离散持有”直接冲突。最小也得二选一：

- 继续让 `Agent` 保留连续 mounted stage 视图，但 scheduler/runtime 内部转成 node registry。
- 或者彻底取消 `Agent` 对 pipeline stage 数组的直接理解，让 `Agent` 只持有 pipeline handle。

第二种更干净，但改动面明显更大。

### 4.4 Bridge 协议冲突

当前 bridge 协议天然偏 stage-name：

- 入边/出边 lookup 用 stage 名；
- debug capture 用 stage 名；
- `stage_container_sets` 的 key 也是 stage 名。

如果 stage 被提升为 node，必须明确：

- node key 是否仍然允许直接退化成 `stage_name`；
- 同一算法在不同 pipeline、不同 owner、不同 stage index 下是否允许重名；
- debugTool 是显示 `stage_name` 还是 `node_id`。

只要存在“同一个算法名在多个 pipeline 或多个 owner 下重复挂载”，单独用 `stage_name` 做 key 就不够了。

### 4.5 迁移路径冲突

当前仓库里存在两套 CPU pipeline 执行逻辑：

- `Agent` 内部还有一套旧的 pipeline tick 主循环。
- `AlgorithmScheduler::TickMountedPipeline` 又有一套调度后的主循环。

这说明仓库正处在“从 agent 内收口到 scheduler/executor”的过渡态。此时再直接上 node 化，如果不先确定以哪一套为准，很容易把重复逻辑扩大成三套。

## 5. 哈希表与哈希值建议

### 5.1 已有工具可以直接复用

仓库里已经有现成的 FNV-1a 64 位哈希实现，而且重复了两份：

- [src/algorithm_management/algorithm_manager.h](/D:/gptsandbox/src/algorithm_management/algorithm_manager.h:346) 到 [src/algorithm_management/algorithm_manager.h](/D:/gptsandbox/src/algorithm_management/algorithm_manager.h:370)
- [src/agent/agent.cpp](/D:/gptsandbox/src/agent/agent.cpp:30) 到 [src/agent/agent.cpp](/D:/gptsandbox/src/agent/agent.cpp:54)

建议不要再写第三份，直接收敛成一个公共 helper，例如：

- `algorithm_management/stage_node_hash.h`
- 或 `common_data/hash_utils.h`

### 5.2 Node Key 建议

不要直接拿“stage 名”做哈希 key，建议最少包含：

- `pipeline_name`
- `owner_agent_name`
- `stage_index`
- `stage_name`

如果后面要支持同 pipeline 多实例或热重挂，再加：

- `pipeline_instance_id`
- 或 `registration_generation`

推荐形状：

```cpp
struct StageNodeKey {
  std::string pipeline_name;
  std::string owner_agent_name;
  uint32_t stage_index{0u};
  std::string stage_name;
};
```

然后提供：

- `bool operator==(const StageNodeKey&, const StageNodeKey&)`
- `struct StageNodeKeyHash`

如果还需要更轻量的索引，再把它压成 `uint64_t stage_node_id`，但展示层仍保留可读 key。

### 5.3 哈希表里建议放什么

如果按你这次目标推进，node 表里建议放“单 stage 持有物”，不要继续把整条 pipeline 塞回去：

- stage 静态定义
- stage 对应算法对象或算法句柄
- stage runtime 状态
- 入边/出边引用
- debug 统计

lane、pending stage0 submission、circular loopback 这些仍然更像 pipeline / lane 级状态，不建议硬塞进单 node。

## 6. 这次改造前必须先定的架构决策

建议先明确下面 4 个问题，否则代码改到一半一定返工：

1. node registry 最终 owner 是 `Agent` 兼容层还是 `AlgorithmScheduler`？
2. `Agent` 后续是继续持有“stage 连续数组视图”，还是只持有 pipeline handle？
3. bridge / transfer map 的 lookup key 是继续用 `stage_name`，还是升级成 `StageNodeKey` / `stage_node_id`？
4. lane 状态是继续 pipeline-owned，还是拆成 node 可见但不归 node 所有？

## 7. 推荐的最小落地顺序

如果我们要尽量少返工，建议顺序是：

1. 先定 owner：先选 `AlgorithmScheduler` 持有 node registry，还是暂时继续让 `Agent` 保留更多兼容 ownership。
2. 先抽公共哈希 helper，把现有 FNV-1a 两份实现收敛。
3. 先定义 `StageNodeKey`、`StageNodeDefinition`、`StageNodeRuntimeState`，只落类型，不改调度逻辑。
4. 再把 scheduler 内部 `pipeline_runtime_states_` 旁边补一层 node registry，先做镜像索引。
5. 等 node registry 稳定后，再改 tick 路径，从“range 驱动”逐步切到“node 驱动”。
6. 最后才改 bridge 协议的 key，从 `stage_name` 升级到 node 级 key。

## 8. 当前建议

结合现有文档和代码，我更建议这样理解这次改造：

- 短期：先把 `AlgorithmScheduler` 从“整条 pipeline runtime owner”收敛成“pipeline/node 注册与路由中心”。
- 中期：让 node registry 成为 scheduler 内的第一层索引，lane/runtime 仍暂时保持 pipeline-owned。
- 长期：继续按 [devtask.md](/D:/gptsandbox/devtask.md) 的方向，把 pipeline 语义收口到 `algorithm_management`，同时让 `runtime_systems` 维持“只提供执行原语”的边界。

这样和现有分层、call chain、以及 executor 化方向都更不冲突。

## 9. 备注

这份检查基于当前本地仓库代码与文档完成。Echo Engine 知识库查询在本环境里被自动审批拦下，因此这里的结论全部以当前 checkout 为准，没有额外引入知识库侧的口径。
