---
name: algorithm-studio-playbook
description: "Teach and operate Algorithm Studio end to end: scene layout, interface4agents command flow, build scripts, and debugTool render preview."
---

# Algorithm Studio Playbook

## What This Skill Teaches

This skill teaches a later agent how to turn a plain algorithm idea into a buildable Algorithm Studio project.

Following it should produce:

- a clear `algorithmDevScene` authoring flow for the five real execution phases
- a separate `helperScene` flow for container, decomposer, and d2c support work
- a repeatable internal-agent command sequence using `interface4agents`
- a buildable package and a launch path into `debugTool`
- a `renderpreview` observation scene for checking the built algorithm visually

## When To Use It

Use this skill when the task is one of these:

- create or revise an Algorithm Studio demo
- teach an internal agent how to build an algorithm inside the studio
- separate algorithm logic from helper logic
- add or inspect render preview behavior
- explain which files and scripts are used during authoring, build, and preview

## Core Model

Treat the workspace as three layers:

1. `algorithmDevScene`
   - the real algorithm development surface
   - use it for the five execution phases
   - keep the phase preference visible and obvious

2. `helperScene`
   - support logic only
   - use it for container, decomposer, and d2c work
   - keep aliasing and packing concerns here, not in the main algorithm flow

3. `renderpreview`
   - observation only
   - use it to inspect the built algorithm or launch `debugTool`
   - do not mix it with authoring logic

## Command Families

Use these command families because they map directly to the editor model:

- `scene`
  - switches the current scene or scene group
  - use it to move between `algorithmDevScene`, `helperScene`, and `renderpreview`

- `v`
  - creates or edits container variables
  - use it when the algorithm needs a scalar or buffer slot

- `function`
  - creates or edits function nodes
  - use it for actual algorithm text or reusable logic

- `phase`
  - creates or edits execution-phase nodes
  - use it for `pretick`, `exec`, `afterrick` (current UI spelling may still appear as `aftertick`), `renderresult`, `reflect`, and `allinone`

- `highlight`
  - marks the exact UI target before a guided step
  - use it only when teaching a user interactively

Why these commands:

- they keep the agent on the same vocabulary as the UI
- they prevent drift between authoring language and build output
- they let the internal agent produce a minimal, inspectable command log

## Internal Agent Build Loop

If an internal agent wants to construct an algorithm by itself, it should follow this loop:

1. Switch to `algorithmDevScene`.
2. Add the minimum containers needed by the algorithm.
3. Add or edit the function node that contains the real algorithm logic.
4. Add phase nodes only when the algorithm needs explicit execution-stage separation.
5. Keep helper-only aliasing or container packing work in `helperScene`.
6. Save the project state.
7. Build the project with the repo script.
8. Launch `debugTool` and inspect the render preview result.

The agent should use scripts for build and launch instead of improvising shell logic inside the scene flow.

## Debug Preview Contract

The build-and-preview path should be:

1. build the algorithm package
2. produce the runtime artifact
3. open `debugTool`
4. inspect the render preview output

The `renderpreview` scene exists to make this loop visible inside Algorithm Studio.

## Reusable Operating Rules

- Keep algorithm creation and helper creation separated.
- Keep the internal agent log short and mechanical.
- Prefer buildable output over speculative explanation.
- If the user asks for feedback, return concrete change points, not abstract praise.
- If the user wants the next agent to reuse the workflow, point it at this skill and its references.
