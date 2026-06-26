# Pipeline Plugin Resolution Conflict

## Date

2026-06-26

## What I verified

I used the built-in pipeline runner as a probe and narrowed the current mount
failure to the first body-stage mount of:

- `v2a0_pipeline_square_vertex_demo`

The runner probe now shows:

- wrapper loading is not the blocker here
- mount stops before the first body stage finishes `BuildAlgorithmMount`
- `LoadAlgorithmInterventionFromLocation(...)` completes successfully
- immediately after that, package resolution reports:
  - `has_plugin=false`
  - `plugin_module_path` is empty

Probe evidence from the current runtime logs:

- `scheduler_mount_probe.log`
  - wrapper is `declared=false`
  - mount stops before `mount.body_stage_built`
- `package_loader_probe.log`
  - `create_from_location.intervention_load.end present=true`
  - `create_from_location.plugin_check.begin has_plugin=false path=`

## Root cause

Current package resolution is hard-wired to:

- `TryResolveAlgorithmPackageLocation(...)`
  - this currently resolves only from `.algo`
  - file:
    [src/algorithm_support/algorithm_package_location.cpp](D:/gptsandbox/src/algorithm_support/algorithm_package_location.cpp:854)

When resolution comes from `.algo`, it sets:

- `runtime_package_root = mounted_package_root`
- plugin lookup candidates:
  - `<mounted_package_root>/Debug/<algorithm>.dll`
  - `<mounted_package_root>/<algorithm>.dll`

But the actual build output I verified is split like this:

- cloud/runtime package:
  - [algorithmLib/algorithmruntimeLib/v2a0_pipeline_square_vertex_demo](D:/gptsandbox/algorithmLib/algorithmruntimeLib/v2a0_pipeline_square_vertex_demo)
  - contains `.algo` and shader/runtime payload
  - does **not** contain the root DLL
- locally built plugin DLLs:
  - `algorithmruntimeLib_verify/.../Debug/*.dll`
  - `algorithmruntimeLib_gpuverify/.../Debug/*.dll`

So with the new fail-fast rule:

- "missing plugin DLL is a hard failure"

the current runtime correctly rejects these packages.

## Why this is a severe conflict

Two requirements are currently incompatible in this workspace:

1. `TryResolveAlgorithmPackageLocation(...)` should remain cloud-first / `.algo`-driven
2. `.algo` packages without DLL must not silently pass

But the current `.algo` extraction layout does not ship the DLL that the runtime
now requires.

So:

- if I keep cloud-only resolution, existing mounted algorithms cannot run
- if I add local DLL fallback from verify/gpuverify outputs, I break the
  "completely rely on cloud" direction

This affects both:

- pipeline algorithms
- ordinary algorithms

because the same package-location rule is shared.

## Confirmed examples

Cloud-extracted cache contains no DLL:

- `D:/gptsandbox/algorithmLib/algorithmruntimeLib/.algo_cache/.../v2a0_pipeline_square_vertex_demo_package.json`
- shader payload exists under `stage0/`
- no `v2a0_pipeline_square_vertex_demo.dll`

Local verify builds do contain DLLs:

- `D:/gptsandbox/algorithmLib/algorithmruntimeLib_verify/v2a0_pipeline_square_vertex_demo/Debug/v2a0_pipeline_square_vertex_demo.dll`
- `D:/gptsandbox/algorithmLib/algorithmruntimeLib_gpuverify/v2a0_pipeline_square_vertex_demo/Debug/v2a0_pipeline_square_vertex_demo.dll`

## Decision needed

Please choose one of these directions:

1. Keep cloud-only resolution.
   Then the package/build pipeline must be changed so extracted `.algo` runtime
   packages include the required DLL beside the package JSON/resources.

2. Allow hybrid resolution.
   Use `.algo` for package JSON/runtime payload, but resolve plugin DLL from the
   local verify/gpuverify compile layout when the cloud package does not include
   it.

3. Relax the hard-failure rule temporarily.
   I do **not** recommend this, because it conflicts with the explicit fail-fast
   requirement you gave.

## Current status

I stopped here because this is no longer a wrapper-only code issue.
It is now a package-resolution policy conflict between:

- cloud-only package ownership
- mandatory plugin DLL enforcement

Once you pick the direction, I can continue immediately.
