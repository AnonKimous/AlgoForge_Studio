# DevTask: Agent / Algorithm / RuntimeSystem 边界收口

## 文档状态

- 本文档用于替代旧 `pipelineDevDoc.txt` 与旧 `devtask.md`。
- 如果历史描述与本文档冲突，以本文档为准。
- 本轮先确认文档口径，确认后再按本文档推进代码修改。

## 背景

当前主干任务聚焦于 pipeline / scheduler / agent / runtime_systems 的分层收口。

与 Python 工具侧相关的算法产物生成、打包、装载规则，不并入这份主干 `devtask`。

## 总目标

### 目标：收紧运行时边界

1. `runtime_systems` 不再承接任何 algorithm 专属概念、类型、owner 语义和调度语义。
2. `Agent` 对算法的了解收口到最小边界：
   - 它知道自己持有 `AlgorithmObject`
   - 它知道这是普通算法还是 pipeline 算法
   - 如果是 pipeline，它只知道拓扑是 `linear` 或 `circular`
   - 除此之外，`Agent` 不主动拆解算法内部 stage / lane / bridge / intervention / reflector 细节
3. `AlgorithmObject` 持有 pipeline 相关信息是允许的；问题不在 `obj` 持有，而在 `Agent` 不应自己动手拆 `obj` 内部结构并驱动细粒度执行。
4. pipeline 的内部推进、顺序、lane、stage runtime 仍属于 `algorithm_management` / `AlgorithmScheduler` 语义，不下沉到 `runtime_systems`。

## 本轮实施范围

### P0：先做边界收口

1. 继续把 `runtime_systems` 内与 algorithm 直接耦合的内容拔掉。
2. 检查并削减 `Agent` 对 pipeline 内部执行细节的直接介入。
3. 保持调用链仍为：
   `sdk -> agent_management -> agent -> algorithm_management -> runtime_systems`
4. 不允许静默失败；状态不一致直接报错或断言。

### P1：整理调试能力与正式运行能力的边界

1. `intervention`、`reflector`、`result render` 属于开发/调试语义，不是正常运行态语义。
2. 在真实运行算法中，默认不要求存在 `intervention` / `reflector`。
3. `Agent` 不应该把底层介入器当成自己的信号控制器；算法信号应由算法自身负责。
4. `result render` 只能留在 `debugTool` 语境下，不能进入 release 运行时。
5. `build release` 时：
   - 必须砍掉渲染器
   - `intervention` / `reflector` 要么被开发者提前收编进算法本体，要么直接被剔除
   - 这轮实现只负责“砍”，不负责自动收编
6. `build debug` 时：
   - 不丢任何调试构件
   - 需要明确说明：带完整调试构件的算法只允许在调试器路径下运行

### P2：统计能力改成显式启用

1. `Agent` 对算法耗时数据只保留一个上层接口。
2. 该接口的职责是“要求输出一份日志”。
3. 只有启用该接口时，底层才开启统计逻辑。
4. 不再把逐 stage / 普通算法耗时统计默认塞进核心热路径。

## 本轮明确不做的事

1. 不在这一轮把所有 stage 全量抽象成 node。
2. 不在这一轮把 `AlgorithmScheduler` 改成按 node 哈希表完全持有。
3. 不在这一轮改掉现有 pipeline 的“两段提交”语义。
4. 不在这一轮改算法底层 `vn/an` 容器规则。
5. 不在这一轮修改 `CMakeLists.txt`。
6. 不在这一轮把 Python 工具侧的产物生成、打包、装载需求并入主干整改范围。

## 当前已确认的架构判断

1. `runtime_systems` 不应该理解：
   - `AlgorithmObject`
   - pipeline
   - stage
   - lane
   - runtime transfer map
   - algorithm owner / 调度语义
2. `AlgorithmScheduler` 当前继续作为 pipeline 语义的主要承接层，是可接受的过渡状态。
3. `lane` 语义本身是合理的，但调度顺序不应该由 `Agent` 自己处理。
4. `Agent` 应把“内部那一大坨”整体视为算法，不应把它拆成“介入器 + 反射器 + 算法本体 + 介入渲染”的多个运行时控制对象。
5. `AlgorithmObject` 持有把管线推送给调度中心所需的信息是正常的，暂时不作为本轮问题源头。

## 临时自定义参数规则

### 1. 三类数据分层

1. stage 内部 scratch
   - 只在当前 stage 执行过程中临时存在
   - 不跨 stage
2. stage 间临时参数
   - 当前 stage 产出，下一 stage 立即消费
   - 只用于少量碎数据传递
   - 不视为 lane 长期主状态
3. lane 长期状态
   - 必须进入 `standard container`
   - 不允许伪装成临时参数通道

### 2. 允许的跨 stage 传递方式

1. 额外 `v` 槽位
   - 通过 `extra_variable_count` / `extra_variable_offset` 一类映射信息描述
   - CPU 路径走共享 `interStageBuffer`
   - GPU 路径走共享 `stageBuffer`
2. 同名同结构的 custom container
   - 仅允许 `custom -> custom`
   - 名称必须相同
   - 结构必须完全一致

### 3. 明确禁止的情况

1. `standard slot <-> custom container` 混传
2. custom container 跨 stage 改名
3. 共享前缀之外新增额外标准 `a`
4. 把 `interStageBuffer` 当成完整 stage 状态副本

### 4. 选择规则

1. 只在当前 stage 内部使用的数据：放 scratch
2. 只需传给下一 stage 的少量临时标量：优先走额外 `v` + offset
3. 必须以容器形状跨 stage 传递的数据：只能走同名同结构 custom container
4. 需要跨多个 tick 的数据：升格为 `standard container`

## 与现状的主要冲突

### 大冲突

1. `Agent` 里仍残留大量 pipeline 内部逻辑，包括 stage 顺序、lane 推进、bridge debug、replay、逐 stage timing 等。
2. 这和“`Agent` 只把内部视为算法整体”的目标存在正面冲突。

### 小冲突

1. `AlgorithmObject` 内仍带有较多 pipeline 元数据。
2. 这部分当前视为可接受历史包袱，不作为第一轮整改重点。

### 延期项

1. stage/node 化改造冲击面太大。
2. 如果未来重启这件事，要在 owner、lookup key、lane 模型、debug 视图统一后再做。
3. 这一轮先不把它混入实施范围。

## 验收口径

### 边界收口验收

1. `runtime_systems` 的公开接口与实现中，不再出现 algorithm 专属 owner/调度语义。
2. `Agent` 不再继续扩大对 pipeline 内部结构的直接控制。
3. 耗时统计改成显式启用，而不是默认常驻。
4. 调试构件和正式运行构件有明确边界。

## 执行顺序建议

1. 先按本文档继续做 `runtime_systems` 去 algorithm 耦合。
2. 再收 `Agent` 对 pipeline 内部执行细节的直接介入。
3. 再把统计能力改成显式启用。
4. 再整理 debug / release 下 intervention / reflector / render 的边界。
