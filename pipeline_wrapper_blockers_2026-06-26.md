# Pipeline Wrapper Blockers

## Date

2026-06-26

## Current state

The basic wrapper skeleton now compiles:

- wrapper metadata was added to mounted pipeline stages
- `wrapper.stage.stageBegin` / `wrapper.stage.stageEnd` package parsing was added
- wrapper begin/end can be materialized as:
  - a normal algorithm package if its `.algo` can be resolved
  - a built-in no-op ordinary algorithm node if no `.algo` is found
- body stage0 ownership was separated from mounted pipeline root ownership

Build verification passed with:

- `build_debugtool.bat`

## Blocker 1: Runtime GPU execution is still anchored to intervention stages

### What exists now

Current runtime GPU execution path still discovers an executable GPU stage from
intervention-stage metadata in:

- [src/algorithm_support/algorithm_runtime_bridge.cpp](D:/gptsandbox/src/algorithm_support/algorithm_runtime_bridge.cpp)

That means the current code path still effectively treats a GPU executable stage
as something attached through intervention-stage semantics.

### What the clarified rule now says

Per the latest clarification:

- an algorithm should conceptually have:
  - `preTick`
  - `exec`
  - `afterTick`
  - `resultRender`
- `exec` must not be empty
- if a shader-execution path is being treated as `afterTick`, that algorithm was
  mounted in the wrong role
- wrapper is the exception because the system may provide a built-in void method
  for empty wrapper begin/end

### Why this blocks completion

As long as runtime GPU execution is still discovered through intervention-stage
specs, the final behavior does not fully match the clarified execution model.

This is larger than wrapper insertion alone.
It is a runtime execution-model correction.

### Decision needed

Please confirm whether I should:

1. keep the current runtime GPU execution path temporarily, and finish wrapper
   work first
2. stop wrapper work and first refactor runtime GPU execution so `exec` becomes
   the true anchor

## Blocker 2: Circular pipeline semantics with wrapper begin/end are still underdefined

### What is already clear

The clarified rule says:

- head/tail should not be counted as part of the circular/non-circular body path
- wrapper begin/end are outer helper nodes
- body stages remain the true pipeline body

### Remaining ambiguity

For a circular pipeline with wrapper begin/end inserted, these questions still
need one canonical answer:

1. Does body-tail loopback happen before wrapper end runs?
2. Should wrapper end observe the body-tail output snapshot only, without
   feeding back into the next circular body iteration?
3. In circular mode, is wrapper begin only used on external admission, while
   internal body-to-body loopback bypasses wrapper begin entirely?

### Why this blocks completion

The current skeleton can mount wrapper begin/end, but a fully correct circular
lane scheduler needs one precise rule here.

Without that rule, I can easily write a scheduler that compiles but has the
wrong runtime behavior.

### Decision needed

Please confirm the intended rule set for circular mode:

1. `body tail -> body head` loopback only
2. wrapper end is observation/aggregation only
3. wrapper begin is external admission only

If yes, I can continue on that basis.

## Blocker 3: Forced-sync completion semantics are only partially implemented

### What the clarified rule now says

Forced-sync mode should behave like:

1. one agent tick admits a lane through the effective begin side once
2. the lane then advances internally without needing a fresh external tick at
   every stage boundary
3. on some later tick, scheduler discovers the lane finished at the effective
   tail and hands the result back to the agent

### What exists now

The current scheduler code still fundamentally behaves like the older
tick-driven stage stepping logic.

Wrapper body offsets are now separated, but the scheduler has not yet been
fully rewritten to match the clarified forced-sync contract.

### Why this blocks completion

This is a scheduler-behavior rewrite, not just a mount-time insertion change.

## Recommendation

The cleanest next step is:

1. confirm Blocker 1:
   whether runtime GPU execution stays temporarily legacy or must be fixed now
2. confirm Blocker 2:
   circular wrapper begin/end behavior
3. confirm Blocker 3:
   whether I should now rewrite forced-sync scheduler semantics in the current
   legacy runtime model, or only after Blocker 1 is fixed
