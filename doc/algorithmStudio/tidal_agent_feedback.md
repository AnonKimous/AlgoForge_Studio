# Tidal Demo Agent Feedback

This run successfully used the embedded internal agent after the command surface was expanded.

## What the agent produced

The agent returned a minimal `interface4agents` script and the project state was applied in-memory:

- `v1` was created with `count=1` and `stride=4`
- `tide_height` was created and updated with `height = sin(v1)`
- six phase nodes were created:
  - `pretick`
  - `exec`
  - `aftertick`
  - `renderresult`
  - `reflect`
  - `allinone`
- each phase node was linked to `tide_height` and `v1`

Final temporary state:

- `containers=1`
- `functions=1`
- `function_texts=0`
- `stages=6`

## What was blocking the agent

Before the fix, the internal agent could not build the demo because `interface4agents` had no executable commands for function or phase creation.

The first blocked response explicitly said the surface only exposed:

- `v`
- `a`
- `field`
- `createCosNode`
- `hang`
- `integrateChild`
- `scene`
- `hotReview`
- `reNameNode`
- `highlight`

That was the main reason the agent stopped.

## Changes that were needed

1. Add `function` commands to `algorithmDevTools/algorithm_studio/bridge/interface4agents.py`.
2. Add `phase` commands to `algorithmDevTools/algorithm_studio/bridge/interface4agents.py`.
3. Fix the internal `_call()` helper in `interface4agents.py` so it forwards keyword arguments.
4. Keep the prompt examples aligned with the new commands so the internal agent can discover them.

## Remaining work

1. Persist the generated demo to the real project file format if you want the result to survive outside the temporary session.
2. Finish the rename away from the old `interventioner` terminology if you want the UI and agent prompt to use only the new phase vocabulary.
3. Add stage-specific behavior, not just stage membership:
   - `pretick` should increment `v1`
   - `exec` or `renderresult` should read `sin(v1)`
   - `aftertick` should handle any post-update work
4. If you want a readable explanation block in the canvas, add a `functiontext` note for `tide_height`.

## Code location notes

- `algorithmDevTools/algorithm_studio/bridge/interface4agents.py`
  - command surface for the internal agent
  - was the main blocker and now contains the new `function` and `phase` handlers
- `algorithmDevTools/algorithm_studio/algorithm_studio.py`
  - runtime already knows how to add/update `function` and `stage` nodes
  - the agent only needed the command surface exposed

