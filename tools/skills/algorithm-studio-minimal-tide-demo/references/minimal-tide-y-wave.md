# Minimal Tide Y-Wave Fast Path

Use this reference when the user wants the shortest possible tide walkthrough.

The goal is not completeness.
The goal is to get one tiny algorithm built and visible as fast as possible.

## Current UI Assumption

Teach against the current Algorithm Studio layout only.

- `containerScene` uses the left `Drag Palette` in `Blueprint` mode by default.
- `interventionerScene` uses the left `Drag Palette` in `Container Tree` mode by default.
- Do not describe legacy add buttons, old side panels, or stale fixed UI positions.
- If a step depends on dragging, tell the user which left-palette section to use right now.

## Canonical Names

- `algorithm_name`: `minimal_tide_fastpath`
- `package_name`: `minimal_tide_fastpath`
- `v1`: wave height input
- `v2`: wave result output
- first `fun`: sine-wave logic
- second `fun`: display shader logic

## The Only Teaching Flow

Follow this exact sequence.

### 1. Create `v1`

```interface4agents
highlight scene container
```

Then tell the user:

- stay in `containerScene`
- use the left `Drag Palette`
- it should already be in `Blueprint` mode
- create `v1`
- `v1` means wave height

### 2. Create `v2`

```interface4agents
highlight v add
```

Then tell the user:

- drag the `v` tile from `Blueprint > Container`
- create `v2`
- `v1` is the input
- `v2` is the output/result

### 3. Create the first `fun`

```interface4agents
highlight createNode function
```

Then tell the user:

- drag `fun` from `Blueprint > ToolNodes`
- create one `fun`
- write a short text script that says the output follows a sine wave

Suggested script text:

```text
Read v1 as the current wave height input.
Write v2 as sin(v1) scaled to a visible tide value.
Keep the logic minimal and continuous.
```

### 4. Stop and confirm the algorithm is already done

Tell the user:

- good job
- the tide algorithm itself is finished
- if they want to see it, go to `interventionerScene`

### 5. Move to intervention and bring in `v2`

```interface4agents
highlight scene interventioner
```

Then tell the user:

- switch to `interventionerScene`
- the left `Drag Palette` should now show `Container Tree` automatically
- drag `v2` from `Algorithm Containers` into that scene
- `v2` now means the computed result buffer

### 6. Create the display `fun`

```interface4agents
highlight createNode function
```

Then tell the user:

- keep using the left palette in the current scene
- drag one `fun` tile into the intervention canvas
- create one more `fun`
- write a display shader
- that shader should fill the lower screen in blue and move with the sine wave

Suggested display-shader text:

```text
Use v2 as the tide height.
Fill the area below the tide line with blue.
Leave the area above it clear.
The tide line should move up and down with the sine-wave result.
```

### 7. Build

Tell the user:

- press `Build`

Do not insert extra arrangement, cleanup, merge, reflector, or decomposer steps.

## What Build Produces

After the build step, summarize the output using these defaults:

- checklist: `references/artifacts/minimal_tide_fastpath_checklist.md`
- package json: `references/artifacts/minimal_tide_fastpath_package.json`
- plugin cpp: `references/artifacts/minimal_tide_fastpath_plugin.cpp`
- function scripts: `references/artifacts/minimal_tide_fastpath_function_scripts.json`
- shaders:
  - `references/artifacts/minimal_tide_fastpath_result_render.vert`
  - `references/artifacts/minimal_tide_fastpath_result_render.frag`
- `.algo` package note: `references/artifacts/minimal_tide_fastpath_algo_package.md`

## Real Project Locations To Mention

When the user asks where the algorithm ends up, point them here:

- source bundle folder:
  - `algorithmLib/algorithmSrc/minimal_tide_fastpath/`
- runtime bundle folder after build:
  - `algorithmLib/algorithmruntimeLib/minimal_tide_fastpath/`
- expected package path:
  - `algorithmLib/algorithmruntimeLib/minimal_tide_fastpath/minimal_tide_fastpath.algo`

Then tell the user:

- the built algorithm can now be inspected from `debugTool`
