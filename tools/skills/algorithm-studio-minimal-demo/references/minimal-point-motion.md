# Minimal Point Motion

Use this reference when the user wants the smallest possible point-motion demo in Algorithm Studio.

## Target Shape

- `algorithm_name`: keep equal to `package_name`
- one descriptor input: `phase01`
- one variable container: `v1`
- one array container: `a1`
- one `afterTick`
- one `resultRender`

Avoid:

- `v2`, `v3`
- `a2+`
- `meshNode`
- `reflector`
- `decomposer`
- extra stages
- extra helper functions

## Expected Semantics

Interpret `phase01` as a normalized motion phase:

- `0.0` means point at `(0, 0, 0)`
- `0.5` means point at `(100, 0, 0)`
- `1.0` means point back at `(0, 0, 0)`

Equivalent formula:

```text
t = clamp(phase01, 0, 1)
triangle = 1 - abs(t * 2 - 1)
x = triangle * 100
y = 0
z = 0
```

## Minimal Structural Intent

- `v1` stores `phase01`
- `a1` stores one xyz point
- `afterTick` transforms `v1 -> a1`
- `resultRender` displays `a1`

## Preferred Authoring Choice

If descriptor binding must be represented and the current UI tools do not express it cleanly, prefer one coherent `update_document` result over many partial tool calls.
