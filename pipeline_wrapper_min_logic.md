# Pipeline Wrapper Minimal Logic

## Goal

Add one wrapper head stage and one wrapper tail stage around a mounted pipeline,
while keeping the mounted pipeline externally visible as an ordinary algorithm.

## Core structure

1. The pipeline body stages stay unchanged in principle.
   `stage0` stays in the body. `stageBegin` is not `stage0`.

2. The wrapper owns two optional ordinary-algorithm-like stages:
   `stageBegin` and `stageEnd`.

3. The wrapper object may exist even when both stages are empty.
   Empty wrapper is legal.

4. If `wrapper.stage.stageBegin` exists, insert it before the pipeline body.

5. If `wrapper.stage.stageEnd` exists, insert it after the pipeline body.

6. Effective mounted order is:
   `stageBegin -> stageBody[0..n-1] -> stageEnd`

7. If `stageBegin` is absent, the effective head is `stageBody[0]`.

8. If `stageEnd` is absent, the effective tail is `stageBody[n-1]`.

## Ordinary algorithm compatibility

1. Wrapper head and tail are still just ordinary algorithm nodes in nature.
   They are special only because they may read/write the whole standard
   container and are inserted by the pipeline wrapper logic.

2. `stageBegin` and `stageEnd` can directly access the whole standard
   container.
   They are not limited to the body-stage runtime transfer mapping view.

3. Ordinary algorithms are allowed to have no reflector.
   Missing reflector is legal.

4. Ordinary algorithms are allowed to have no intervention.
   Missing intervention is legal.

5. Wrapper stages are also allowed to have no reflector.
   Wrapper stages are also allowed to have no intervention.

6. Empty wrapper stages must not change old behavior.
   If no wrapper stage exists, mounted behavior should match the original
   pipeline behavior as closely as possible.

## Reflection and intervention ownership

1. Body stages remain ordinary algorithms.
   Each body stage may still carry its own local reflector and local
   intervention just like before.

2. Whole-pipeline reflector/intervention responsibility belongs to the wrapper.

3. Do not use `intervention.stages` for whole-pipeline behavior.
   Whole-pipeline wrapper stages are loaded from `wrapper.stage`.

4. The agent should keep using the mounted pipeline like a normal algorithm.
   No special submit entry, reflection entry, or intervention entry should be
   exposed just because the algorithm is internally a pipeline.

## Scheduler responsibility

1. Scheduler is responsible for pushing work into the mounted pipeline and
   collecting finished output from the effective tail.

2. For non-forced mode, scheduler behavior stays export-oriented.
   It mainly watches the pipeline output side and takes whatever completed data
   is available.

3. After adding wrapper tail, the non-forced output watch position must move to
   the effective tail.
   That means:
   If `stageEnd` exists, watch `stageEnd`.
   If `stageEnd` does not exist, watch the last body stage.

4. For forced mode, scheduler should no longer require every internal stage to
   wait for a fresh external tick signal.

5. Forced mode desired behavior is:
   On one agent tick, scheduler admits a lane through the effective head once.
   After that admission, the lane continues to advance internally without
   waiting for a new agent tick on every stage boundary.
   On some later tick, scheduler observes that the lane has finished at the
   effective tail, collects the result, and returns it back to the agent.

6. In forced mode, the only external admission wait point is the effective
   begin side.
   That means:
   If `stageBegin` exists, wait there once.
   If `stageBegin` does not exist, wait at `stageBody[0]` once.

7. In forced mode, result collection happens only at the effective end side.
   That means:
   If `stageEnd` exists, collect there.
   If `stageEnd` does not exist, collect from the last body stage.

## Package loading requirements

1. Wrapper stages are loaded from `wrapper.stage.stageBegin` and
   `wrapper.stage.stageEnd`.

2. If a wrapper stage entry exists, load it and insert it.

3. If a wrapper stage entry does not exist, do not insert it.

4. A runtime `.algo` package must contain its plugin DLL.
   Missing DLL is a hard failure and must assert/fail fast.

5. Missing reflector alone is not a failure.

6. Missing intervention alone is not a failure.

## Practical implementation scope

Only touch the parts needed for:

- package loading of `wrapper.stage`
- mount-time insertion of wrapper head/tail around body stages
- scheduler/tick wiring needed to execute wrapper head/tail in order
- forced sync admission/advance/collect semantics
- non-forced output watch relocation to the effective tail
- reflection/intervention exposure so the mounted pipeline still looks like a
  normal algorithm from the agent/high-level view
