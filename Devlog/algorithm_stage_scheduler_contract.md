# Algorithm Stage Scheduler Contract

## 1. Goal

This document records the intended scheduling model for algorithm execution in this project.

The scheduler holds the `AlgorithmObject` specifically so it can inspect the algorithm's stage layout, decide how to batch stages, and control synchronization between batches.

## 2. Five Logical Stages

An algorithm is modeled as up to five logical stages:

- `pretick`
- `exec`
- `aftertick`
- `reflect`
- `renderresult`

The scheduler must be able to determine which of these stages exist and which are empty.

## 3. Stage Ownership

The stage sources are:

- `pretick`: provided by the intervention package
- `aftertick`: provided by the intervention package
- `renderresult`: provided by the intervention package
- `exec`: provided by the algorithm body itself
- `reflect`: provided by the reflector

## 4. Stage Availability Rules

The availability rules are:

- `exec` is mandatory.
- If `exec` is missing, the scheduler must fail immediately.
- `pretick` may be empty.
- `aftertick` may be empty.
- `reflect` may be empty.
- `renderresult` may be empty.

The scheduler must know which stages are empty before it decides how to submit work.

## 5. Execution Preference Rules

Each logical stage has its own execution preference.

Current expected preferences:

- `pretick`: CPU or GPU
- `exec`: CPU or GPU
- `aftertick`: CPU or GPU
- `reflect`: CPU only
- `renderresult`: GPU only

`pretick` and `aftertick` are not yet used in a fully detailed way today, but the interface must keep their preference fields.

If a stage is absent, it does not need to be written into the manifest.

## 6. Scheduler Responsibility

The scheduler must:

- parse the `AlgorithmObject` and extract the stage sequence
- detect which logical stages are present
- reject the algorithm if `exec` is missing
- group adjacent stages when their execution preference is compatible
- split stage submission when the preference changes
- wait for the previous batch to finish before submitting the next batch when synchronization is required
- control synchronization between CPU and GPU execution paths
- control synchronization between standard container mappings and lane-owned containers
- perform packing inside the scheduler so the executor sees the smallest possible task bundle
- only decide whether the current bundle changes; the scheduler does not need to reason about every isolated stage once a bundle is formed

## 7. Batching Rules

### 7.1 Stages With The Same Preference

If multiple consecutive stages share the same execution preference, the scheduler must batch them together when that batch boundary is supported by the sync mode.

Example:

- `pretick` = GPU
- `exec` = GPU
- `aftertick` = GPU
- `renderresult` = GPU

In this case, the scheduler must submit the consecutive GPU stages together.

The scheduler should always try to reduce the number of submitted tasks.

Examples:

- `cpu`, `cpu`, `null`, `null`, `cpu` must be packed into one CPU task
- `gpu`, `gpu`, `gpu`, `gpu`, `null` must be packed into one GPU task
- `null`, `cpu`, `gpu`, `null`, `cpu`, `cpu`, `cpu`, `cpu`, `null`, `cpu` should be packed as three bundles:
  - one CPU bundle with synchronization
  - one GPU bundle with synchronization
  - one final CPU bundle that may be merged with the next algorithm when the next bundle is also CPU-side

The scheduler only needs to check whether the bundle changes.

Here `null` means the stage is absent and does not need a separate submission.

### 7.2 Stages With Different Preferences

If the preference changes across stages, the scheduler must split the work and provide synchronization itself.

Example:

- `pretick` = CPU
- `exec` = GPU
- `aftertick` = CPU
- `renderresult` = GPU
- `reflect` = CPU

In this case, the scheduler cannot push the whole object into one executor call.

It must:

- submit `pretick`
- wait for `pretick` completion
- submit `exec`
- wait for `exec` completion
- submit `aftertick`
- and so on

The scheduler must treat a GPU batch boundary followed by a CPU batch boundary, or a CPU batch boundary followed by a GPU batch boundary, as a hard split point that requires synchronization.

The packer lives inside the scheduler.

Its job is to look at the current stage stream, build the smallest valid task bundle, and stop the bundle whenever the execution side changes.
The scheduler only decides whether the current pack continues or splits.

## 8. Pipeline Algorithms

Pipeline algorithms are a serial composition of multiple algorithms.

They follow the same stage contract.

If multiple consecutive pipeline stages have GPU preference, the scheduler must submit them as one GPU batch when that boundary is valid for the current sync mode.

If the pipeline has mixed preferences, the scheduler must split the pipeline into compatible batches and synchronize between them.

In forced synchronization mode, if the wrapper, the body algorithms, and all of their stages are GPU-side, the scheduler may pack them into one large GPU bundle and submit that bundle as a whole.

In non-forced synchronization mode, the packing decision depends on the wrapper and the currently executing stage.
The scheduler still only compares the current bundle against the next bundle boundary.
Here `stage` means one algorithm inside the pipeline, because a single algorithm inside a pipeline is also called a stage.
After that algorithm finishes, it returns to the scheduler and waits for the next tick.

For pipeline algorithms, the synchronization object includes the whole standard container and the bridge, not just one isolated stage result.

### 8.1 Wrapper Rules

The wrapper has special scheduling rules.

The wrapper is always scheduled, in both forced synchronization mode and non-forced synchronization mode.

The wrapper has its own independent rules, such as `beginstage` continuously listening and waiting to open a new lane at any time.

If the wrapper rules conflict with the existing scheduler rules, do not force a local rewrite.
Report the conflict instead.

## 9. Lane And Bridge Forms

`Lane` is the runtime execution form of pipeline algorithm data.

On the CPU side, lane data appears as the algorithm object's containers.

On the GPU side, lane data appears as the submitted standard container set.

`Bridge` is the same idea in another form.

On the CPU side, bridge data appears as the buffer.

On the GPU side, bridge data appears as the implicit container plus mapping table attached to the standard container set.

## 10. Bridge And Container Synchronization

For both normal algorithms and pipeline algorithms, the scheduler is responsible for bridge synchronization.

This means:

- standard containers and lane containers are both owned by the lane side of the runtime
- the scheduler must keep the implicit standard-container mapping in sync with the actual buffer contents
- bridge copy and buffer propagation are scheduler concerns, not executor concerns
- in execution paths that switch CPU and GPU ownership, the bridge must move together with the lane state

Under ideal conditions, in forced synchronization mode, the pipeline must behave like:

- bind resources once
- bind stage0, draw
- bind stage1, draw
- bind stage2, draw

This is forced synchronization mode.

This is the ideal case.

Real pipeline algorithms may still contain CPU and GPU stage switches inside the same forced-sync submission, such as `exec` followed by `reflect`.

In that case, the scheduler must still block and wait for the current stage to finish before advancing.

In non-forced synchronization mode, the lane must finish a stage, return to the scheduler, and then wait for the next tick before continuing.

## 11. Summary

The intended model is:

- `AlgorithmObject` is not a single opaque execution unit
- it is a five-stage semantic object
- the scheduler understands stage existence, stage emptiness, and stage preference
- the scheduler batches compatible stages according to sync mode
- the scheduler splits incompatible stages
- `exec` is mandatory
- `pretick`, `aftertick`, `reflect`, and `renderresult` may be absent
