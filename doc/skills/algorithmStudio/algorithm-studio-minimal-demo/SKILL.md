---
name: algorithm-studio-minimal-demo
description: Build the smallest valid Algorithm Studio demo bundle with strict scope control, interface4agents-only execution, and optional internal container refinement. Use when a user asks for a minimal demo algorithm, a descriptor-driven motion example, or a single-point movement demo.
---

# Algorithm Studio Minimal Demo

Build the minimum valid scene that satisfies the request. Prefer fewer nodes, fewer stages, and fewer files over flexibility.

## Execution Contract

- Read [references/minimal-point-motion.md](references/minimal-point-motion.md) for the canonical point-motion target.
- Use `interface4agents` only.
- Do not emit `algorithm-studio-tool`.
- Do not use `update_document`.
- If the prompt includes an operation stack, read it before choosing the next step.
- If the user wants a guided walkthrough, teach one UI step at a time and emit exactly one supported `highlight` before that step.
- Keep all teaching text in English unless the user explicitly asks for another language.

## Workflow

1. Read the current scene and keep existing unrelated content unchanged unless the user asked to replace it.
2. Reduce the request to the minimum data path needed to satisfy it.
3. Prefer direct `interface4agents` commands for scene changes.
4. Stop as soon as the requested demo is representable. Do not add optional helpers.

## Hard Rules

- Do not invent extra `v`, `a`, `meshNode`, `reflector`, `decomposer`, `interventioner`, or `fun` nodes unless the request cannot work without them.
- If the scene uses readable aliases, keep them as authoring semantics only and still use `vN/aN` names in runtime-facing explanations.
- Keep `algorithm_name` and `package_name` identical if the user will likely build the result.
- Prefer one `afterTick` stage and one `resultRender` stage for motion demos.
- Use `fun` only when real plugin logic is explicitly requested.
- Keep one `functiontext` only when the user explicitly wants detached natural-language solution text.
- Treat containers as storage-only. Container bit width alone does not define float, int, fp8, or bool semantics.
- If the user needs finer structure, expand `v/a` and refine them with internal layout fields.
- Layout field `ruleText` is a free-form contract such as `from v1 to phase01 32` or `from a1 to x,y 16,16`.

## Canonical Minimal Motion Shape

For a single point that moves from `(0, 0)` to `(100, 0)` and back, prefer this shape unless the user asks otherwise:

- one descriptor input: `phase01`
- one variable container: `v1`
- one array container: `a1`
- one `afterTick`
- one `resultRender`

Map the data path like this:

- descriptor `phase01` -> `v1`
- `afterTick` reads `v1`
- `afterTick` writes one point into `a1`
- `resultRender` draws `a1`

If the user wants `a1` split into smaller parts, teach layout fields inside the expanded container instead of claiming that the container itself owns a hardcoded scalar type.
