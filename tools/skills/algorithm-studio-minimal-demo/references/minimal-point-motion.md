# Minimal Point Motion

Use this reference when the user wants the smallest possible point-motion demo in Algorithm Studio.

## Target Shape

- keep `algorithm_name` equal to `package_name`
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
- if a readable alias is shown in the tool, keep it tool-side only and still explain the runtime-facing slots as `v1` / `a1`
- `v1` and `a1` only describe storage shape and width
- do not explain them as owning a fixed scalar type unless the current algorithm explicitly adds that interpretation

## Optional Internal Layout Teaching

If the user wants the point container refined into smaller readable parts, expand `a1` and add layout fields.

Example commands:

```interface4agents
field add a1 variable x 32 "from a1 to x 32"
field add a1 variable y 32 "from a1 to y 32"
field add a1 variable z 32 "from a1 to z 32"
```

Important:

- these field rules are the current algorithm's read contract
- they are not a universal statement that the container itself is permanently `float32`

## Preferred Authoring Choice

- prefer direct `interface4agents` commands
- keep the scene as small as possible
- if the user wants a live walkthrough, emit one `highlight` before each supported step and use the operation stack when it is available
