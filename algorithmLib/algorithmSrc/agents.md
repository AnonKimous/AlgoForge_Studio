# Algorithm Bundle Rules

## Naming

- Use `v` for variable containers.
- Use `a` for array containers.
- Keep bundle names short and explicit.

## File Roles

For each algorithm, there are four file groups:

- `algodevdoc`
  - Developer document only.
  - Read-only.
  - Must not be mounted.
  - Must not be executed.
- source tree
  - Implementation source only.
  - Source-side manifests live here as `manifest.json`.
  - Read-only to the loader and runtime.
  - Must not be mounted.
  - Must not be executed.
- `.algo`
  - Runtime bundle.
  - Readable by loader and runtime.
  - Eligible for mounting and execution.
- `algocache`
  - Runtime cache stored next to `.algo`.
  - Readable by loader and runtime.
  - Eligible for mounting and execution.

Loader, decomposer, reflector, and execution code must only read `.algo` and `algocache`.
They must not read source files or `algodevdoc`.
`algodevdoc` is documentation only. It is not a build input, a mount input, or an execution input.
At execution time, the algorithm contract only supports `.algo` and `algocache`.

## Pipeline Bridge Contract

- `wrapper` only provides the outer pipeline shell.
- `wrapper` must not define bridge semantics.
- Bridge semantics belong to the explicit manifest mapping between adjacent algorithms.
- Use explicit remaps such as `v1 -> bridge0` on the upstream side and `bridge0 -> v2` on the downstream side.
- `catalog` may normalize or materialize these explicit mappings during package creation.
- `catalog` must not infer missing mappings.
- `runtime_systems` must consume the finalized package only.

## Container Naming

Use the standard layout format:

`v{variable_count}a{array_count}_{purpose}`

- `vN` means the bundle exposes `N` variable containers named `v1` through `vN`.
- `aN` means the bundle exposes `N` array containers named `a1` through `aN`.
- The suffix after the standard name describes the bundle purpose.

## Example

- `temporary_test_line_motion`
  - `v1`, `v2`, `v3`: initial point positions
  - `a1`: moving point buffer

## Bundle Rules

- Keep the folder name, manifest name, and catalog entry name identical.
- Keep the prefix counts exact.
- Put the behavior description after the `vNxM` standard name.
- Use explicit names that make the purpose obvious.
