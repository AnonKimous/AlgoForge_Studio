---
name: scheduler-runtime
description: Explain and safely modify the current algorithm scheduler runtime, including pipeline registration, stage grouping, lanes, ownership, runtime state, and tick progression. Use when changing scheduler behavior, pipeline lifetime, stage execution, lane state, or scheduler diagnostics.
---

# Scheduler Runtime

Use the current source as the authority. Read these files before changing scheduler behavior:

- `src/algorithm_management/algorithm_scheduler_runtime.h`
- `src/algorithm_management/algorithm_scheduler_runtime.cpp`
- `src/agent_management/agent.cpp`
- `src/agent_management/agent_manager.cpp`
- `src/algorithm_management/algorithm_manager.h`
- `src/algorithm_catalog/algorithm_protocol.h`

## Current ownership model

The public call path is:

`sdk -> agent_management -> agent -> algorithm_management -> runtime_systems`

`Agent::SubmitAlgorithm` groups mounted pipeline stages and calls `TickMountedPipeline`. The scheduler owns pipeline registration and runtime state. The agent owns the mounted algorithm objects and presents stage groups to the scheduler. Runtime systems execute jobs, Vulkan, or CUDA work after the scheduler has built the stage work.

Do not move algorithm semantics into the scheduler. The scheduler coordinates execution, stage order, lane state, runtime lifetime, and diagnostics. It must not infer particle counts, draw counts, or algorithm behavior from container sizes.

## Pipeline registration and state

The current scheduler entry points are:

- `AlgorithmScheduler::SubmitAlgorithmObject`
- `AlgorithmScheduler::RegisterPipeline`
- `AlgorithmScheduler::RegisterPipelineRuntime`
- `AlgorithmScheduler::TickMountedPipeline`
- `AlgorithmScheduler::TryGetPipelineRuntime`
- `AlgorithmScheduler::UpdatePipelineRuntime`

Pipeline state uses `JobsPipelineRuntimeState`, `JobsPipelineLaneRuntimeState`, and stage runtime statistics. Mounted stage objects carry `pipeline_name`, `pipeline_stage`, `pipeline_stage_index`, and `pipeline_stage_count`.

When inspecting a pipeline, verify that mounted stages are contiguous, have the same pipeline name, and have consecutive stage indices. Do not identify a lane only by pipeline name: lane state is associated with its submitting owner.

## Tick flow

The scheduler path has three conceptual phases:

1. Stage ingress prepares the stage input state.
2. Stage execute submits the selected backend work.
3. Stage egress publishes the stage result and updates pipeline state.

`TickMountedPipeline` maintains stage and lane progress, invokes stage work, updates runtime statistics when the selected synchronization mode requires them, and returns the updated pipeline state to the agent layer. Inspect the actual function body before assuming whether a code path is synchronous or asynchronous.

## Lifetime

Package loading supplies an algorithm tick lifetime. The loader defaults to continuous lifetime and supports an explicit launch-once/hold mode through the package runtime section. Do not unload a mounted algorithm merely because one phase completed. Only an explicit execution-end condition may end a runtime.

## Debugging checklist

When a scheduler change behaves unexpectedly:

1. Inspect the mounted stage group in `agent.cpp`.
2. Inspect pipeline registration and lane lookup in `algorithm_scheduler_runtime.h`.
3. Inspect the runtime transfer map separately through the decomposer/manifest skill.
4. Check `testData\pipeline\debugInfo\last_run.log` and the newest file in `artifacts\pipeline_timing`.
5. Run the matching runner after the final build. A compile-only check is not sufficient.

Do not add silent recovery paths for invalid stage topology or unexpected runtime state. Existing project instructions require unexpected states to fail directly.
