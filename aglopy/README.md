# aglopy

`aglopy` is a small Python wrapper around the repository's `debugTool` runner protocol. It lets a Python script start a persistent runner server and submit ordinary algorithms or pipelines without manually composing PowerShell commands.

## Example

```python
from pathlib import Path

from aglopy import DebugToolRunner

repo = Path(__file__).resolve().parents[1]

with DebugToolRunner(repo) as runner:
    normal = runner.run_algorithm(
        "v6a6_pbd_ball_collision_demo",
        execution="jobs",
        preview_output=repo / "testData" / "aglopy_collision.ppm",
    ).require_ok()
    print(normal.response, normal.preview_pixels)

    pipeline = runner.run_pipeline(
        "v4a16_fireworks_pipeline_demo",
        execution="vk",
        preview_output=repo / "testData" / "aglopy_fireworks.ppm",
    ).require_ok()
    print(pipeline.response, pipeline.preview_pixels)
```

The default debugTool path is `build/Debug/debugTool.exe`. Pass `debugtool=...` to `DebugToolRunner` when using another build.

`RunnerServer` is persistent, so multiple algorithm and pipeline submissions can share one debugTool process. All generated preview files should be placed under `testData/`.
