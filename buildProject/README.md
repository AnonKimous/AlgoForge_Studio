# Build Project

Anaconda or Miniconda is mandatory for every entry point. Activate a conda environment before running these commands; regular system Python, bundled workspace Python, and unactivated conda interpreters are refused. Python 3.10+ invokes CMake; CMake selects the platform generator and compiler. SDK builds are intentionally outside this package.

From the repository root, install the Python requirements first:

```text
conda activate algoforge
python -m pip install -r requirements.txt
```

## Microsoft toolchain

```text
conda activate algoforge
python boot/booterMSVC.py v6a6_pbd_ball_collision_demo
```

The Microsoft entry points use Visual Studio 2022 on Windows and write the mainline build to `build/Microsoft`. The toolchain file defines `ALGOFORGE_TOOLCHAIN_MSVC=1`.

## Open-source toolchain

```text
python boot/booterNinjaClang.py v6a6_pbd_ball_collision_demo
```

The open-source entry points use LLVM clang-cl plus Ninja on Windows, and clang plus Ninja on other platforms. The toolchain file defines `ALGOFORGE_TOOLCHAIN_CLANG=1` and writes the mainline build to `build/OpenSource`.

Algorithm packages use the repository's standard runtime output location, so building an algorithm replaces that algorithm's existing package for the selected configuration.

## Remove untracked files

```text
python buildProject/clean.py
```

This runs `git clean -fdx` and preserves `buildProject`. It also removes ignored build products, runtime packages, and test artifacts. Review the command before running it.
