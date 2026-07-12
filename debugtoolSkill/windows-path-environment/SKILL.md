---
name: windows-path-environment
description: Build and run this repository on the main Windows development machine where the PATH environment is unreliable. Use when cmake, Ninja, Visual Studio tools, PowerShell, or algorithm packaging cannot be found even though they are installed.
---

# Windows Path Environment

This repository has a known machine-specific problem: the `PATH` environment name may be unusable during builds. Windows environment names are case-insensitive, but the project batch files deliberately preserve the working `Path` spelling.

## Required environment pattern

The repository build scripts use this pattern:

```bat
set "ORIG_PATH=%Path%"
set "PATH="
set "Path=%ORIG_PATH%"
```

When a command cannot be found, try the `Path` variable rather than rebuilding the command or editing a build script. In PowerShell, inspect both `$env:Path` and `$env:PATH`, but do not permanently rewrite the user's machine environment as part of a repository task.

## Build entry points

Do not edit `CMakeLists.txt` or build scripts to work around this machine issue. Use the existing entry points:

```powershell
cmd /c build_debugtool.bat
cmd /c build_algorithm_releaseWithDebugInfo.bat <algorithm>
cmd /c build_sdk.bat
```

The algorithm build script locates Visual Studio with `vswhere`, initializes `VsDevCmd.bat`, sets the repository LLVM `clang-cl`, locates Visual Studio's `NINJA_EXE`, and packages the selected algorithm. If a repository `build_local` file exists, inspect it and use it according to its instructions.

## Direct packaging diagnosis

If the runtime cache script reports that Ninja is unavailable, do not change the script. Initialize the Visual Studio developer environment first and provide `NINJA_EXE` from the Visual Studio CMake Ninja directory, then rerun the existing script or the dedicated algorithm build batch. Keep packaging logs under `testData`.

## Rules

- Preserve UTF-8 source and documentation encoding.
- Never modify third-party library source.
- Never change `CMakeLists.txt` without explicit approval in the current turn.
- Do not create root-level transient logs or test outputs.
- After the final successful build, run the matching debugTool runner and inspect its result.
