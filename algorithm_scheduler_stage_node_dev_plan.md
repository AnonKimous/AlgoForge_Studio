# Dev Plan: AlgorithmScheduler Stage/Node 化改造

## 状态

- 状态：deferred
- 结论：当前阶段不实施
- 原因：改动面过大，涉及 `Agent`、`AlgorithmScheduler`、pipeline runtime、bridge 协议、lane 状态、debug/stall 视图的统一收口，当前阶段不具备安全落地条件

## 当前共识

1. 暂时不做“stage 全量抽象成 node，并统一由调度器管理”的改造。
2. 当前 pipeline 继续沿用“连续 stage 段 + pipeline/lane runtime state”的主模型。
3. `runtime_systems` 不承接 algorithm / pipeline / stage 语义，后续还要继续把现有相关内容拔掉。
4. `Agent` 对 pipeline 的认知边界收紧为：
   - 它知道这是 pipeline 算法
   - 它知道拓扑是 linear 还是 circular
   - 除此之外不再扩展对内部 stage / lane / bridge 的理解
5. stage 间临时自定义参数继续沿用当前规则：
   - 少量临时标量优先走额外 `v` + offset
   - 自定义容器只允许 `custom -> custom` 同名同结构映射
   - lane 主状态继续归 `standard container`

## 为什么先挂起

这次改造的风险不是单点代码修改，而是模型切换风险：

1. 当前真实流动的是 lane 状态，不是单独的 stage/node。
2. `Agent` 仍按 `pipeline_name + pipeline_stage_index` 识别一整段 pipeline，这已经超出了目标边界。
3. `AlgorithmScheduler` 当前持有的是 pipeline runtime，不是 node registry。
4. bridge / transfer 协议当前按 `stage_name` 路由，不是按 `node_id` 路由。
5. `runtime_systems` 里仍残留 algorithm 专属接口和类型引用，这已经超出了目标边界。
6. debug、stall、timing 目前也主要按 pipeline group 视角成立。

如果在这些前提都没收口之前强上 node 化，很容易形成多套真相并存：

- 文档按 node 说
- `Agent` 按连续 stage 段跑
- scheduler 按 pipeline runtime 存
- bridge 按 `stage_name` 路由

这会让后续维护成本显著升高。

## 以后再做时的目标

如果未来恢复这项工作，目标不是“把 stage 放进哈希表”这么简单，而是同时完成下面几件事：

1. 先把 `runtime_systems` 中 algorithm 专属接口和类型彻底拔掉
2. 再把 `Agent` 对 pipeline 的认知边界收紧到“pipeline 算法 + linear/circular”
3. 定义稳定的 `StageNodeKey`
4. 明确 node registry owner
5. 明确 lane 如何在 node 图上推进
6. 把 bridge key 从 `stage_name` 升级成 node 级身份
7. 统一 debug / stall / timing 的 node 视图

## 恢复实施前的前置条件

恢复这项开发前，至少要先满足下面条件：

1. 先完成 `runtime_systems` 和 algorithm 的边界切耦合
2. 先完成 `Agent` 对 pipeline 边界的收口
3. 再决定 node registry 最终由谁持有
   - 候选：`algorithm_management::AlgorithmScheduler`
   - 候选：极薄的 `Agent` 兼容层
4. 再决定 bridge / transfer map 的 lookup key 是否从 `stage_name` 升级
5. 再决定 lane runtime 是继续 pipeline-owned，还是拆成 node 可见模型
6. 再决定 debugTool 展示的是 stage 顺序视图、node 图视图，还是双视图

在这些问题没拍板前，不进入实现阶段。

## 恢复后的推荐顺序

未来如果重新启动，建议按这个顺序做：

1. 先切 `runtime_systems` 和 algorithm 的 ABI / include 耦合
2. 再切 `Agent` 对 pipeline 内部 stage 细节的直接依赖
3. 只补类型，不改调度逻辑
   - `StageNodeKey`
   - `StageNodeDefinition`
   - `StageNodeRuntimeState`
4. 抽公共哈希 helper
   - 收敛当前重复的 FNV-1a 实现
5. 给 scheduler 增加 node 镜像索引
   - 先镜像，不替换现有 pipeline runtime
6. 再改 tick 路径
   - 从 range 驱动逐步切到 node 驱动
7. 最后再改 bridge 协议和 debug 视图

## 当前阶段明确不做的事

1. 不在这一轮把 `Agent` 继续做成理解更多 pipeline 细节的层
2. 不在这一轮把 `runtime_systems` 继续做成理解 algorithm 的层
3. 不改 `AlgorithmScheduler` 当前的 pipeline runtime 持有方式
4. 不改 bridge 当前的 `stage_name` 路由协议
5. 不引入新的 node runtime 存储结构到正式执行路径

## 关联文档

- 现状与冲突检查：[algorithm_scheduler_stage_node_refactor_conflict_check.md](/D:/gptsandbox/algorithm_scheduler_stage_node_refactor_conflict_check.md)
- 当前统一口径：[devtask.md](/D:/gptsandbox/devtask.md)

## 一句话结论

这项改造已经确认“方向上可讨论，但当前阶段不实施”，先作为延期开发计划挂起；现阶段继续维护现有 pipeline 模型，不把精力投入到 stage/node 总体重构上。
