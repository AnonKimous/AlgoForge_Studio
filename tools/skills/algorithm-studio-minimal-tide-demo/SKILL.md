---
name: algorithm-studio-minimal-tide-demo
description: Teach the fastest possible minimal tide demo in Algorithm Studio. Use when the user wants a very short walkthrough that creates one executable tide algorithm, then adds the smallest visible intervention path and build step.
---

# Algorithm Studio Minimal Tide Demo

Teach the user the shortest working tide path.

Do not add extra structure, cleanup, arranging, or side explanations.

## Required Reading

- Read [references/minimal-tide-y-wave.md](references/minimal-tide-y-wave.md) before teaching.
- Use the artifact examples in `references/artifacts/` when the user asks what files build produces.

## Execution Contract

- Use `interface4agents` only.
- Advance one UI step per reply.
- If the operation stack is available, read it before choosing the next step.
- Keep the flow brutally short.
- Do not add mesh nodes, reflector nodes, decomposer nodes, extra container groups, or extra cleanup steps unless the user explicitly asks for them.
- Do not describe legacy UI positions or old add-button flows.
- In the current UI, `containerScene` defaults the left `Drag Palette` to `Blueprint`, while `interventionerScene` defaults it to `Container Tree`.
- When teaching drag actions, describe the current left palette mode and the node tile the user should drag, not an old fixed panel position.

## Mandatory Teaching Order

Use this exact fast path:

1. Tell the user to create `v1`.
   `v1` means wave height.
2. Tell the user to create `v2`.
   `v1` is the input and `v2` is the output/result.
3. Tell the user to create one `fun` node and write a short sine-wave text script.
4. Tell the user the algorithm itself is already done.
   If they want visualization, point them to `interventionerScene`.
5. Tell the user to switch to `interventionerScene`, then bring `v2` into that scene.
   Tell them this now comes from the left `Container Tree`.
   `v2` now means the computed result buffer.
6. Tell the user to create one more `fun` node and write a display shader that fills the lower screen in blue and follows the sine wave.
7. Tell the user to press `Build`.

## Highlight Rule

Before each supported step, emit exactly one `highlight`.

The highlight should match the current UI layout.
When needed, explain the live location in words after the highlight:

- `highlight scene container` points at the `containerScene` tab.
- `highlight v add` points at the left `Drag Palette > Blueprint > Container > v`.
- `highlight createNode function` points at the left palette `fun` tile in the current mode.
- `highlight scene interventioner` points at the `interventionerScene` tab.

Good examples:

```interface4agents
highlight scene container
```

```interface4agents
highlight v add
```

```interface4agents
highlight createNode function
```

```interface4agents
highlight scene interventioner
```

## What To Say After The Build Step

After the user reaches build, summarize only these deliverables:

- checklist
- package json
- plugin cpp
- shader files
- `.algo` package location
- where to inspect the algorithm in `algorithmLib`
- that the user can now open `debugTool` and look for the built algorithm

Use the sample files under `references/artifacts/` as the default output shape.
