# Pipeline GPU Exec Schema Conflict

## Date

2026-06-26

## What is already fixed

The previous cloud-package blocker is fixed:

- `.algo` packaging now includes the required plugin DLL
- mounted pipeline package resolution now finds DLLs inside `.algo_cache`
- pipeline mount succeeds
- resource-batch submission succeeds

Probe evidence now shows:

- `attach.agent_manager_mount.end index=0`
- `mount.end begin_index=0`
- `resource_batch_submitted`

So the package/DLL issue is no longer the current blocker.

## New confirmed blocker

The mounted pipeline now reaches the first real execution step and stops at the
first body stage:

- stage: `v2a0_pipeline_square_vertex_demo`

Runner probe sequence:

- `tick.ingress.begin stage=v2a0_pipeline_square_vertex_demo`
- `tick.ingress.end stage=v2a0_pipeline_square_vertex_demo`
- `tick.execute.begin stage=v2a0_pipeline_square_vertex_demo`

Then `SubmitAlgorithmObject(...)` resolves the execution branch as:

- `submit.branch=missing_gpu_exec stage=v2a0_pipeline_square_vertex_demo`

That means the runtime does **not** see a valid executable GPU stage for this
algorithm package.

## Why this happens

Current runtime GPU execution anchor is still determined by intervention-stage
metadata from:

- [src/algorithm_support/algorithm_runtime_bridge.cpp](D:/gptsandbox/src/algorithm_support/algorithm_runtime_bridge.cpp)

Specifically, GPU execution is only recognized when intervention metadata
matches the old runtime expectation.

But the current package content for the mounted demo does not expose an
explicit runtime GPU `exec` stage in the clarified sense.

So after the package/DLL fix, the next real blocker is now exactly the schema
gap that had been suspected before:

- package is mounted
- package has intervention metadata
- package has DLL
- but package still has no recognized executable GPU `exec` stage

## Why this is a severe conflict

Your clarified rule says:

1. algorithm conceptually has:
   - `preTick`
   - `exec`
   - `afterTick`
   - `resultRender`
2. `exec` must not be empty
3. if shader execution is being treated as `afterTick`, the algorithm is
   mounted wrong

The currently mounted legacy GPU pipeline packages do not yet satisfy that
schema.

So there is now a direct conflict between:

- the clarified execution model
- the existing package schema/content for legacy GPU algorithms

## What I verified

This is not a mount failure anymore.
It is not a wrapper failure anymore.
It is not a missing-DLL failure anymore.

It is specifically an execution-schema failure at first GPU stage execution.

## Decision needed

Please choose one direction:

1. **Schema migration now**
   Introduce/consume explicit `exec` metadata and migrate legacy GPU packages
   to expose it correctly.

2. **Temporary legacy compatibility**
   Keep the clarified long-term model, but temporarily allow current legacy GPU
   packages to map their existing stage metadata into runtime executable `exec`
   discovery.

I stopped here because this is now a real architecture decision, not just a
local bug fix.
