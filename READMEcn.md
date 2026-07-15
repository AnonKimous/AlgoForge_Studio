# AlgoForge Studio

面向算法模块化装配、调试、运行与打包的通用算法生产平台。

AlgoForge 将算法组织成可描述、可封装、可挂载、可执行、可调试、可预览的算法包，主要由 SDK、debugTool 和 Algorithm Studio 工具链组成。

## 快速开始

项目必须使用 Anaconda/Miniconda 的 conda 环境。首次使用：

```bat
conda create -n algoforge python=3.11
conda activate algoforge
python boot\quick_begin.py
```

已有环境则从下面开始：

```bat
conda activate algoforge
python boot\quick_begin.py
```

`quick_begin.py` 会安装仓库所需的 Python 依赖。未激活 conda，或使用系统 Python/bundled Python 时，入口会拒绝执行。

## 构建

使用 MSVC：

```bat
python boot\booterMSVC.py <algorithm_name>
```

使用 Ninja + LLVM clang-cl：

```bat
python boot\booterNinjaClang.py <algorithm_name>
```

算法参数可省略，此时只构建主干和 SDK。构建完成后，`boot\debugTool.exe` 指向所选构建的调试工具。

启动 Algorithm Studio：

```bat
python -m pip install -r algorithmDevTools\algorithm_studio\requirements.txt
python boot\launch_devTools.py
```

## 执行阶段与偏好

算法包按阶段组织：`pretick`、`exec`、`aftertick`、`renderResult` 和 `reflect`。其中 `exec` 是实际执行逻辑，不能为空。

每个阶段可以声明执行偏好：

- `jobs`：CPU/任务执行路径。
- `vk`：Vulkan/GPU 路径。
- `cuda`：CUDA/GPU 路径。

`renderResult` 固定使用 `vk`，`reflect` 固定使用 `jobs`；其他阶段从包清单读取偏好，未声明时按运行时阶段默认值处理。选择 `cuda` 不等于选择 clang 编译器：它是算法运行时的后端偏好，算法包还必须提供对应的 CUDA 执行组件。当前仓库的 CUDA 接口和调度路径已存在，但 CUDA 不是项目级默认执行路径，也不应把 clang 构建入口理解成 CUDA 构建入口。

## Algorithm Studio

Algorithm Studio 是算法包的可视化编辑和 AI/教学辅助工具，负责节点、容器、连接关系和包 JSON 的编辑导出。它当前是 UI 优先的原型；仓库的 C++、Vulkan 和 CUDA 代码生成/构建仍由算法开发者和根目录 Python 构建入口负责，不能把 Studio 的导出误认为已经完成编译。

算法开发的一般流程是：在 Studio 中整理算法包结构并导出 JSON，准备或编写对应的 `cpp`、shader、`cu` 资源，再使用 `boot\booterMSVC.py` 或 `boot\booterNinjaClang.py` 构建和验证。

## Pipeline 算法

项目支持把多个普通算法阶段组织成一个 pipeline 算法。每个 stage 仍然是独立的算法包，通过标准容器、阶段提交和容器映射在运行时连接；调度器负责 stage 顺序、跨阶段数据传递、同步和运行时统计。

仓库中的实际示例是 `v4a16_fireworks_pipeline_demo`，位于 `algorithmLib\algorithmSrc\pipeline\`，包含 `stageBegin`、`stage0` 到 `stage4` 以及 `stageEnd`。它的总 `manifest.json` 和各 stage 的清单共同描述阶段资源与连接关系，可以通过 debugTool 的 pipeline runner 验证。

因此，普通算法和 pipeline 算法是两种组织层级：普通算法提供单个可执行模块，pipeline 算法负责把多个模块编排成有数据流和执行顺序的组合算法。

## 项目结构

- `boot/`：Python 启动与构建入口。
- `buildProject/`：CMake 构建编排、toolchain 自动获取和打包逻辑。
- `algorithmLib/`：算法源代码、清单和运行时包生成目录。
- `src/`：SDK、debugTool、算法管理和运行时核心代码。
- `sdk/`：构建后导出的 SDK。
- `demo/`：演示资源。
- `doc/`：skill、开发记录、项目愿景和其他项目文档。
- `testData/`：测试日志、预览文件和 pipeline timing 输出。

## 文档

- [项目文档索引](doc/README.md)
- [项目愿景](doc/VISION.md)
- [Algorithm Studio 文档](algorithmDevTools/algorithm_studio/README.md)
- [构建说明](buildProject/README.md)

## 项目现状

项目仍处于早期开发阶段，部分功能和文档会持续调整。当前重点是完善算法包生产、pipeline 调度、可视化调试、SDK 集成和跨平台构建能力。
