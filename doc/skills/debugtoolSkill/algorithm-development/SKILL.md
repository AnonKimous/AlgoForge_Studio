---
name: algorithm-development
description: Explain the algorithm catalog, package files, development documents, build paths, mounting workflow, protocol locations, and performance validation for this repository.
---

# Algorithm Development

Use this skill when inspecting, building, mounting, or profiling an algorithm package. Treat the current source tree, the algorithm loader, and the checked-in build scripts as the authority. Older README files and Algorithm Studio notes may describe an entry point that no longer exists.

## 1. Algorithm catalog

The repository does not use a separate hand-maintained algorithm registry as the primary catalog. Source packages are discovered under:

```text
algorithmLib\algorithmSrc\norm\<algorithm-name>\manifest.json
algorithmLib\algorithmSrc\pipeline\<pipeline-name>\manifest.json
```

A pipeline may contain stage packages with their own manifests:

```text
algorithmLib\algorithmSrc\pipeline\<pipeline-name>\stage1\manifest.json
algorithmLib\algorithmSrc\pipeline\<pipeline-name>\stageEnd\manifest.json
```

The manifest is the runtime-facing package contract. It declares the package name, available execution backends, containers, decomposers, reflectors, stages, shaders, and result-render behavior. Pipeline root manifests also declare stage relationships and mappings.

Generated runtime packages are separate from the source catalog:

```text
algorithmLib\algorithmruntimeLib\norm\<algorithm-name>\
algorithmLib\algorithmruntimeLib\pipeline\<pipeline-name>\
```

Do not edit generated `.algo`, `.dll`, `.spv`, `.pdb`, or similar files. Change the source package and rebuild it. When a catalog looks inconsistent, inspect `src\algorithm_catalog` package-path and loader code, then compare the source manifest with the generated package. Do not invent a new registration file merely because a UI list is stale.

## 2. Package files

| File | Role |
|---|---|
| `manifest.json` | Runtime/package contract: containers, stages, shader bindings, execution support, and render declarations. |
| `default.json` | Default resource bindings and descriptor values used when the package is mounted. |
| `*_plugin.cpp` | Optional native plugin implementation. It can provide Jobs, Vulkan, CUDA, bundle, or reflector support through the public interfaces. |
| `*.vert`, `*.frag` | GLSL shader sources compiled into SPIR-V during packaging. |
| `*.spv` | Generated shader binaries in runtime output. |
| `*.algo` | Generated runtime package/archive consumed by the runtime. |
| `*.dll`, `*.lib`, `*.exp`, `*.pdb`, `*.ilk` | Generated native plugin and build/debug artifacts. |
| `*_algo.md` | Human-facing development note. It is not the runtime ABI. |
| `*_algoDevDoc` | Algorithm Studio serialized development document and UI/model state. It is not a substitute for the manifest. |
| `*_function_scripts.json` | Algorithm Studio/function metadata or script descriptions. Verify against runtime source before treating it as executable behavior. |

Pipeline packages use these file types at the root and inside stage directories. A stage owns its own stage manifest and implementation files. Do not assume that a container or binding exists in every stage; follow the manifest and pipeline mapping graph.

## 3. Development documents versus source

The development document records Algorithm Studio design/model state and helps reconstruct a package. Source files implement native execution, shader execution, and package behavior. `manifest.json` is the runtime contract consumed by decomposition, package loading, stage construction, and execution setup.

If they disagree, use runtime code and the manifest schema as the contract. Update the development document only when documentation consistency is part of the requested change. Do not treat an old markdown note, skill reference, or GUI label as proof that a build command or protocol still exists.

The public algorithm declarations are in:

```text
src\algorithm_catalog\algorithm_abi.h
src\algorithm_catalog\algorithm_protocol.h
algorithmLib\algorithmSrc\algorithm_plugin_api.h
```

Read these files before changing a plugin or manifest binding. The scheduler and decomposer skills explain how the host consumes these declarations.

## 4. Build paths

### Verified Ninja plus clang-cl path

From the repository root:

```powershell
python boot\booterNinjaClang.py <algorithm-name>
```

For example:

```powershell
python boot\booterNinjaClang.py v4a16_fireworks_pipeline_demo
```

This Python booter initializes Visual Studio, selects the repository or user-cache LLVM/`clang-cl` toolchain, uses Ninja, builds shader and plugin artifacts, and invokes the runtime packaging workflow to produce the runtime package. This is the current verified algorithm build path. Read `doc/skills/debugtoolSkill/windows-path-environment/SKILL.md` first if the machine has the known PATH problem.

### MSVC path

`algorithmLib\CMakeLists.txt` supports a Visual Studio/MSVC CMake configuration and adds UTF-8 compiler options for MSVC. Its important cache inputs are:

```text
ALGORITHM_LIBRARY_SOURCE_ROOT
ALGORITHM_LIBRARY_RUNTIME_OUTPUT_ROOT
CORE_BUILD_DIR
BUILD_ALGORITHM_SAMPLE_PLUGIN
BUILD_ALGORITHM_CUDA_SAMPLE_PLUGIN_ALGOS
```

For an MSVC workflow, open `algorithmLib` as a CMake project in Visual Studio 2022, configure those cache paths, select the generated algorithm target (`algorithm_algos` is the aggregate target), and build the required algorithm target. Do not modify `algorithmLib\CMakeLists.txt` to work around a local build.

The checked-in MSVC Python booter uses Visual Studio/MSBuild for `debugTool` and the SDK. The clang Python booter is the verified algorithm-package build command.

### Stale build references

The build entry points are `boot\booterMSVC.py` and `boot\booterNinjaClang.py`. Algorithm Studio File -> Build invokes the clang Python booter.

## 5. Mounting

1. Build the package so runtime output exists under `algorithmLib\algorithmruntimeLib`.
2. Start `debugTool` from the repository root so relative package and resource paths resolve.
3. Select or request the algorithm by its manifest/package name.
4. For a pipeline, ensure the root package and every referenced stage runtime package are present.
5. Run one deterministic runner tick before relying on a GUI preview.

The Algorithm Studio preview actions now invoke `debugTool.exe` directly. The CLI and runner endpoint workflow is documented in `doc/skills/debugtoolSkill/debugtool-cli-runner/SKILL.md`; the runner remains the reproducible validation path.

## 6. Algorithm protocol

Read `algorithm_abi.h` and `algorithm_protocol.h` before implementing a package. The plugin include shim is:

```cpp
#include "../algorithm_plugin_api.h"
```

An ordinary native plugin commonly implements `IAlgorithmJobsExecutor` and returns a bundle through `AlgorithmPlugin_CreateBundle`. Vulkan-capable packages additionally describe `AlgorithmVkExecSpec` stages, shader paths, and positional container bindings. Manifest declarations, plugin bindings, shader declarations, and container layouts must agree.

The host supplies the execution environment. The algorithm owns its semantics and data:

- The mainline must not infer object, particle, fireworks, or draw counts from array sizes.
- The mainline must not read an algorithm-owned count field to decide how many objects to render.
- The algorithm decides its data quantity and rendering submission.
- Keep bridge rules separate from mapping rules. A bridge is not a mapping table, and a bridge must not copy a complete container when the standard container slot can be used directly.
- Keep algorithm containers active while the algorithm remains mounted, unless the algorithm explicitly declares an end condition.
- For pipelines, use manifest mappings and stage declarations as the alignment contract; do not add host-side semantic inference.

The exact lifetime, Jobs, Vulkan, CUDA, intervention, reflection, and container types are defined by the ABI/protocol headers. Do not copy an interface signature from an old development note.

## 7. Performance validation

Build the final package first, then use the runner from the repository root. Keep logs and preview files under `testData`:

```powershell
cmd /c start "" /b build\Microsoft\RelWithDebInfo\debugTool.exe --runner-server --runner-server-once --runner-endpoint 127.0.0.1:0
Start-Sleep -Seconds 2
cmd /c build\Microsoft\RelWithDebInfo\debugTool.exe --algorithm-runner --algorithm <ordinary-algorithm> --ticks 1 --execution jobs --preview-output testData\algorithm_preview.png
```

For a pipeline:

```powershell
cmd /c start "" /b build\Microsoft\RelWithDebInfo\debugTool.exe --runner-server --runner-server-once --runner-endpoint 127.0.0.1:0
Start-Sleep -Seconds 2
cmd /c build\Microsoft\RelWithDebInfo\debugTool.exe --pipeline-runner --algorithm <pipeline-name> --ticks 1 --execution jobs --preview-output testData\pipeline_preview.png
```

When Vulkan is in scope, repeat with `--execution vk`. Require `OK algorithm_runner` or `OK pipeline_runner`; output ending at `runner_client.begin` is not a test result.

Inspect the newest:

```text
testData\norm\debugInfo\last_run.log
testData\pipeline\debugInfo\last_run.log
testData\pipeline_timing\<newest timing CSV>
```

The timing CSV gives per-stage elapsed time and is the first place to look for a stalled stage or a stage consuming the tick budget. It proves only that the measured tick completed. For drawable preview work, also require an exported preview, nonzero `preview_pixels`, and ready pipeline/target state. `pipeline=not_ready`, `target=not_ready`, or `preview_pixels=0` is a failure even when the runner returned `OK`.

Compare Jobs and Vulkan separately; do not infer visual frame rate from one CPU-side timing number. Use the CLI/debugTool performance commands in `doc/skills/debugtoolSkill/debugtool-cli-runner/SKILL.md` when a full pipeline report or CSV export is needed.

## 8. Completion checklist

- The package is under `algorithmLib\algorithmSrc\norm` or `algorithmLib\algorithmSrc\pipeline`.
- Manifest, plugin, shader, default, and development-document roles were checked before editing.
- The package was built with the verified repository command or a deliberately configured Visual Studio CMake build.
- Generated runtime output is present and was not hand-edited.
- The algorithm was selected by manifest/package name.
- The relevant runner returned `OK` after the final build.
- The latest log and timing CSV were inspected.
- Preview output is nonzero and ready when rendering is part of the package.
- No host-side count inference, container-copying bridge, or other algorithm semantic was added.
