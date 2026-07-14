# Boot

Anaconda or Miniconda is mandatory. A regular system Python, the bundled workspace Python, or an unactivated conda environment is refused.

## Setup

Use the Python 3.10+ interpreter from an activated conda environment:

```text
conda activate <your-environment>
python -m pip install -r requirements.txt
python boot/quick_begin.py
```

Compile with Microsoft Visual C++:

```text
python boot/compiler_with_msvc.py v6a6_pbd_ball_collision_demo
```

Compile with Ninja and LLVM clang-cl:

```text
python boot/compiler_with_ninja_clang.py v6a6_pbd_ball_collision_demo
```

The algorithm argument is optional. Without it, only the mainline and SDK are built.

After a successful build, `boot/debugTool.exe` and `boot/devTools.py` are refreshed as links to the selected build and the Algorithm Studio launcher.

Open those shortcuts directly from the `boot` directory after the build.

The generated SDK is under `sdk/python` and `sdk/cpp`. Use `sdk/python/aglopy` from Python to submit algorithms and pipelines through the debugTool runner.
