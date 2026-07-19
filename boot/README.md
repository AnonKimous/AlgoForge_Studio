# Boot

Anaconda or Miniconda is mandatory. A regular system Python, the bundled workspace Python, or an unactivated conda environment is refused.

## Setup

Use the Python 3.10+ interpreter from an activated conda environment. For the first setup, run:

```text
conda create -n algoforge python=3.11
conda activate algoforge
python -m pip install -r requirements.txt
python boot/quick_begin.py
```

`quick_begin.py` installs the Python requirements and automatically builds the mainline, SDK, and every algorithm package. Windows uses MSVC by default; pass `OpenSource` for the LLVM/Ninja route. Other supported platforms use LLVM/Ninja by default.

```text
python boot/quick_begin.py OpenSource
```

If the `algoforge` environment already exists, start with `conda activate algoforge`.

Compile with Microsoft Visual C++:

```text
python boot/booterMSVC.py v6a6_pbd_ball_collision_demo
```

Compile with Ninja and LLVM clang-cl:

```text
python boot/booterNinjaClang.py v6a6_pbd_ball_collision_demo
```

`booterNinjaClang.py` resolves LLVM 22.1.8 from `ALGOFORGE_TOOLCHAIN_ROOT`, a shared sibling cache, or the user toolchain cache. If it is not available, it downloads the pinned LLVM installer from the lock file, verifies its SHA256, and installs it into the user cache. LLVM is not stored in this repository.

On Windows, the OpenSource build still uses the installed Windows SDK for `rc.exe` and `winres.h`; the Python build entry point locates that SDK and configures its include paths automatically. The SDK is not downloaded into the repository.

The algorithm argument is optional. Without it, the mainline, SDK, and every algorithm package are built. Pass one algorithm name when you want to build only that package.

After a successful build, launch the real executable at `build/Microsoft/RelWithDebInfo/debugTool.exe` on Windows. Launch Algorithm Studio with `python boot/launch_devTools.py`.

The generated SDK is under `sdk/python` and `sdk/cpp`. Use `sdk/python/aglopy` from Python to submit algorithms and pipelines through the debugTool runner.
