# 2026-05-30 架构整理记录

这份文档只记录今天的实际改动和当前已经收敛下来的主线，不替代主 README。

## 今天做了什么

今天的核心目标是把消息层去冗余，并把“谁负责什么”重新压清楚。

### 1. `msg` 层收缩为纯搬运层

`messaging` 现在只保留这些职责：

- 申请 buffer
- 申请 fast channel
- 读写 buffer 内容
- 复制 packet

已经移走的内容包括：

- 协议路由
- 信号翻译
- 专用运行控制包的编解码
- 业务侧的 dispatch 逻辑

这意味着 `msg` 不再理解业务，不再决定某个 packet 应该怎么翻译、怎么分发，只负责把数据搬到可访问的位置。

### 2. 运行状态同步改为上层直接处理

以前通过消息层发运行控制包的路径已经收掉了。

现在主流程里直接同步：

- `PhysRunState`
- `guide_enabled`

这部分由上层 orchestration 负责，不再借消息层来绕一圈。

### 3. 交互/解码职责继续外移

`decomposition` 层继续负责把上层传来的描述和脏数据拆成算法能吃的数据结构。

今天保留的方向是：

- 描述符在上层组装
- 具体拆解由 `decomposition` 处理
- `msg` 只负责传输，不负责理解

### 4. 物理流程里减少倒挂

`PhysAgent` / `PhysModeController` 现在不再依赖消息层去解析运行控制协议。

物理相关的数据流更接近现在的目标：

- 上层同步运行状态
- 交互侧通过快通道送数据
- 物理解码在本层完成
- 最终提交给更底层的算法或求解流程

## 当前消息层的定位

`messaging` 现在的定位很简单：

- 管 buffer
- 管快速通道
- 提供 buffer id 和 endpoint
- 复制 packet

它不再承担：

- 业务协议解释
- 业务路由
- 算法输入拼装
- 上层状态机控制

## 今天验证过的结果

- 项目可以继续编译
- `min_vulkan_win32.exe` 可以成功生成
- 构建后仍然有 `pwsh.exe` 的环境提示，但这不是这次整理引入的新失败点

## 当前可以继续收的方向

如果后面继续收架构，优先级建议是：

1. 继续检查 `msg` 层还有没有残留的多余封装
2. 继续清理 `agent` 里只是在转发、没有真正承担职责的代码
3. 继续理顺 `decomposition` 和 `algorithm` 的边界
4. 再统一检查各层 namespace 和目录是否完全对应

## 2026-06-01 追加记录

今天继续做了两条主线的整理：

### 1. 术语统一成 `entity`

- 原来的 `instance_interaction` 统一改成了 `entity_interaction`
- `InstanceInteractionRuntime` / `InstanceInteractionUiBridge` 改成了 `EntityInteractionRuntime` / `EntityInteractionUiBridge`
- `main` 不再直接构造摄像机算法包，只通过 `entity_interaction::CreateCameraEntityInfo(mesh)` 创建实体
- CMake 里的库目标也跟着改成了 `entity_interaction`

### 2. 把 ImGui 接回运行时系统

- 新增了 `runtime_systems/render/imgui_vulkan_runtime.h/.cpp`
- 这一层负责 ImGui 的 context、SDL3 backend、Vulkan backend、每帧绘制和提交
- `WindowAgent` 现在会自动初始化并驱动这个 runtime
- `SdlWindow::ProcessEvents()` 恢复把 SDL 事件喂给 ImGui

### 3. 当前对 ImGui 的定位

- `runtime_systems` 只负责把 ImGui 跑起来
- `entity_interaction` 负责决定“画什么”
- 不同 `entity` 以后可以挂不同的 `debugUI`
- `ImGui` 不直接认识 `algorithm` 或 `agent`

### 4. 验证结果

- `cmake --build build` 已通过
- `min_vulkan_win32.exe` 已成功生成
