---
name: algorithm-studio-minimal-demo
description: Build the smallest valid Algorithm Studio demo bundle with strict scope control. Use when a user asks for a minimal demo algorithm, a descriptor-driven motion example, a single-point movement demo, or when Algorithm Studio keeps inventing extra nodes, stages, or scaffolding and must be constrained to the smallest working scene.
---

# Algorithm Studio Minimal Demo

Build the minimum valid scene that satisfies the request. Prefer fewer nodes, fewer stages, and fewer files over flexibility.

## Mode Contract

If the prompt says `Phase: planning only.`, use plan mode:

- do not edit the scene
- do not emit tool calls
- write a short execution document for the later execution pass
- compress long context and long document state before planning further
- prefer this structure:
  - `Goal`
  - `Constraints`
  - `Minimal structure`
  - `Steps`
  - `Execution focus`

If the prompt says `Phase: execution.`, use execution mode:

- read the latest plan first
- perform only the smallest coherent batch of edits
- avoid reopening the whole design unless the plan is clearly broken
- report what changed and what remains

For this skill, the plan document should preserve:

- the target minimal motion shape
- the exact containers and stages allowed
- the descriptor binding intent
- the nodes and files that must not be added

## Workflow

1. Read the current scene and keep existing unrelated content unchanged unless the user asked to replace it.
2. Reduce the request to the minimum data path needed to satisfy it.
3. Prefer UI tools for simple node edits.
4. Use `update_document` when descriptor bindings or a tightly-coupled minimal package are easier to express safely as one coherent document update.
5. Stop as soon as the requested demo is representable. Do not add optional helpers.

## Hard Rules

- Do not invent extra `v`, `a`, `meshNode`, `reflector`, `decomposer`, `interventioner`, or `fun` nodes unless the request cannot work without them.
- Keep `algorithm_name` and `package_name` identical if the user will likely build the result.
- Prefer one `afterTick` stage and one `resultRender` stage for motion demos.
- Use one `fun` only when real plugin C++ is explicitly requested.
- Keep one `functiontext` only when the user explicitly wants natural-language solution text.
- If a descriptor input is required, prefer a single descriptor variable over multiple split descriptors.
- If a point can be represented with one array entry, do not create additional array rows.

## Canonical Minimal Motion Shape

For a “single point moves from `(0,0)` to `(100,0)` and back” demo, prefer this exact shape unless the user asks otherwise:

- one descriptor input: `phase01`
- one algorithm variable: `v1`
- one algorithm array: `a1`
- `a1` stores one point as `x y z`
- one `afterTick`
- one `resultRender`

Map the data path like this:

- descriptor `phase01` -> `v1`
- `afterTick` reads `v1`
- `afterTick` writes one point into `a1`
- `resultRender` draws `a1`

Treat `phase01` as normalized in `[0, 1]` unless the user says otherwise. For back-and-forth motion, use a triangle-wave style mapping so:

- `phase01 = 0.0` -> `x = 0`
- `phase01 = 0.5` -> `x = 100`
- `phase01 = 1.0` -> `x = 0`

Keep `y = 0` and `z = 0`.

## Preferred Package Decisions

- Prefer `update_document` for descriptor-bound demo bundles because the package-level relationship is usually clearer and less error-prone than scattered UI tool edits.
- Keep the package unified in one `<algorithm_name>_package.json`.
- If shaders are required, keep them minimal and only large enough to show the point clearly.
- If a plugin is required, keep it to the minimum runtime code needed to populate or transform the one point.

## References

- For the canonical minimal point-motion target, read [references/minimal-point-motion.md](references/minimal-point-motion.md).
