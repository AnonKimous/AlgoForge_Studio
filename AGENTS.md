# AGENTS.md instructions for D:\gptsandbox

## 约束

- 不允许修改 `CMakeLists.txt`。
- 算法库编译请走仓库根目录的专用批处理工具，不要直接改构建脚本。
- 不允许修改 GBK 编码；如果项目使用 GBK 就固定 GBK，如果使用 UTF-8 就固定 UTF-8，不要尝试修改项目的编码方式。

## Architecture Guardrails

- Do not modify `CMakeLists.txt` unless the user has explicitly and personally approved that change in this turn.
- Keep the call chain aligned with `sdk -> agent_management -> agent -> algorithm_management -> runtime_systems`.

## Coordinate Convention

- Use a left-handed coordinate system.
- The origin `[0,0,0]` is the lower-left near corner.
- `+X` points right, `+Y` points up, and `+Z` points into the screen.
- Vulkan viewport setup in this repository is flipped to match this convention.
- Render Preview shaders interpret `x` and `y` as preview-page pixel coordinates.
- In Render Preview, `[0,0]` is the lower-left corner of the ImGui content region below the title bar.

## Encoding Rule

- Use UTF-8 for source files, comments, UI text, and build output.
- Windows builds pass `/utf-8` through the compiler so source and execution text stay in UTF-8.
