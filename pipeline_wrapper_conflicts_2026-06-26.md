# Pipeline Wrapper Conflicts

## Date

2026-06-26

## Status

Stop-and-wait conflict note.
Do not continue wrapper-stage implementation until these points are confirmed.

## Conflict 1: `intervention.stage` vs `intervention.stages`

### Observed facts

1. Tool-side builder currently expects singular `intervention.stage`:
   [tools/algorithm_studio/algorithm_studio.py](D:/gptsandbox/tools/algorithm_studio/algorithm_studio.py)
   around the build path checks:
   `manifest.get("intervention", {}).get("stage", {})`

2. Runtime-side JSON parser currently reads plural `intervention.stages`:
   [src/algorithm_support/algorithm_intervention_support_detail.h](D:/gptsandbox/src/algorithm_support/algorithm_intervention_support_detail.h)

3. Existing example packages in `algorithmLib` still mostly use plural
   `intervention.stages`, for example:
   [algorithmLib/algorithmSrc/v2a0_pipeline_square_vertex_demo/v2a0_pipeline_square_vertex_demo_package.json](D:/gptsandbox/algorithmLib/algorithmSrc/v2a0_pipeline_square_vertex_demo/v2a0_pipeline_square_vertex_demo_package.json)

### Why this blocks wrapper work

`wrapper.stage` belongs to the same family of package schema decisions.
If I continue now, I may implement wrapper loading against the wrong style and
immediately diverge from the actual authoring pipeline.

### Decision needed

Choose one of these:

1. Canonical schema is singular:
   `intervention.stage`
   `wrapper.stage`

2. Canonical schema is plural:
   `intervention.stages`
   `wrapper.stages`

3. Transitional dual-read is temporarily allowed.

## Conflict 2: GPU executable intervention stage kind is inconsistent

### Observed facts

1. Current runtime GPU bridge treats executable GPU stage as
   `PostExecution + afterTick/aftertick`:
   [src/algorithm_support/algorithm_runtime_bridge.cpp](D:/gptsandbox/src/algorithm_support/algorithm_runtime_bridge.cpp)

2. Existing package examples still describe GPU shader stages mainly as
   `resultRender`, for example:
   [algorithmLib/algorithmSrc/v2a0_pipeline_square_vertex_demo/v2a0_pipeline_square_vertex_demo_package.json](D:/gptsandbox/algorithmLib/algorithmSrc/v2a0_pipeline_square_vertex_demo/v2a0_pipeline_square_vertex_demo_package.json)

3. The wrapper design doc currently assumes:
   `stageBegin` is mainly `preTick`
   `stageEnd` is mainly `afterTick` and `resultRender`

### Why this blocks wrapper work

If `stageEnd` is supposed to own afterTick/resultRender, I need to know whether
runtime GPU execution is anchored to:

1. `afterTick`
2. `resultRender`
3. both, with different responsibilities

Without this, `stageEnd` may mount successfully but never execute the expected
GPU-side stage.

### Decision needed

Define the canonical executable GPU stage kind for runtime:

1. `afterTick`
2. `resultRender`
3. split rule:
   `afterTick` for runtime compute-like GPU execution
   `resultRender` only for preview/render path

## Conflict 3: Wrapper-stage package shape is not yet discoverable in repo

### Observed facts

1. I did not find any existing checked-in package using `wrapper.stage`.
2. I did not find a checked-in parser or builder branch that already defines the
   exact nested JSON structure for `stageBegin` / `stageEnd`.

### Why this blocks wrapper work

I can infer behavior from the design doc, but I cannot safely infer the exact
package schema shape without risking a second incompatible implementation.

### Decision needed

Confirm the exact package shape.

Suggested canonical shape if you want me to proceed from scratch:

```json
{
  "wrapper": {
    "stage": {
      "stageBegin": {
        "...": "begin-stage payload"
      },
      "stageEnd": {
        "...": "end-stage payload"
      }
    }
  }
}
```

Then please also confirm what payload each wrapper stage contains:

1. only intervention
2. only reflector
3. both reflector and intervention
4. full ordinary-algorithm-like payload with future executor support

## Recommendation

The cleanest route appears to be:

1. Normalize package schema to singular:
   `intervention.stage`
   `wrapper.stage`

2. Define `stageBegin` as the owner of `preTick`.

3. Define `stageEnd` as the owner of `afterTick` and `resultRender`.

4. Define runtime GPU executable stage ownership explicitly before wrapper code
   changes continue.

5. After that, I can continue implementing:
   wrapper parsing,
   mounted begin/body/end insertion,
   forced/nonforced scheduler semantics,
   and agent-side normal-algorithm reflection/intervention access.
