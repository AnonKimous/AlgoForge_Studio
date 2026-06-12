# Algorithm Library Naming Notes

Use `v` for variables and `a` for arrays.

## Standard Name Format

`v{variable_count}a{array_count}_{purpose}`

- `vN` means the standard layout exposes `N` variable containers named `v1` through `vN`.
- `aN` means the standard layout exposes `N` array containers named `a1` through `aN`.
- The suffix after the standard name describes what the algorithm bundle does.

## Example

- `v6a2_triangle_collision_runtime_test`
  - `v1` and `v2`: external vertex position
  - `v3` and `v4`: external vertex velocity
  - `v5` and `v6`: runtime control and collision state
  - `a1`: triangle vertex buffer
  - `a2`: triangle velocity buffer

## Bundle Rules

- Keep the folder name, manifest name, and catalog entry name identical.
- Keep the prefix counts exact.
- Put the behavior description after the `vNxM` standard name.
- Prefer short, explicit bundle names that make the purpose obvious.
