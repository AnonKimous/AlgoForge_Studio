# Algorithm Studio Agent Guide

## Scope

This `tools` workspace hosts the Algorithm Studio editor, its agent bridge, and its teaching skills.

- Main desktop app: `tools/algorithm_studio/algorithm_studio.py`
- Agent bridge: `tools/algorithm_studio/agents.py`
- Agent prompt builder: `tools/algorithm_studio/agent_client.py`
- UI command surface: `tools/algorithm_studio/interface4agents.py`
- Teaching skills:
  - `tools/skills/algorithm-studio-minimal-demo/`
  - `tools/skills/algorithm-studio-minimal-tide-demo/`

Read the relevant skill and reference file before teaching.

## Mandatory Execution Model

- Agents must drive the editor through fenced `interface4agents` blocks only.
- `algorithm-studio-tool` blocks are disabled.
- `update_document` is disabled.
- Fail fast on invalid state. Do not silently skip missing nodes, unknown scenes, or unsupported commands.
- Keep all teaching and project-facing documentation in English unless the user explicitly asks for another language.

## Operation Stack Rules

- The operation stack is the latest source of truth for user progress.
- The stack is included in agent context only when the UI toggle `Read Stack: On` is enabled.
- If a teaching flow depends on stack-driven progression and `Read Stack` is off, ask the user to enable it.
- Highlight operations and chat operations are intentionally excluded from the operation stack.
- Scene switches should be treated as real progress signals when they appear in the stack.

## Teaching Rules

- Teach one concrete UI step at a time.
- Do not dump the whole workflow in a single answer.
- When the current command set supports it, emit exactly one `highlight` before explaining the next UI action.
- After the highlighted command block, explain only the current step.
- Before moving forward, read the latest operation stack and confirm that the previous step actually happened.
- If the stack does not confirm the step, stay on that same step and re-highlight only that target.

## Scene Model

Use the current scene names exposed by `interface4agents`:

- `scene main`
- `scene container`
- `scene decomposer`
- `scene reflector`
- `scene interventioner`
- `scene pretick`
- `scene aftertick`
- `scene render`
- `scene d2c`
- `scene all`

For teaching, refer to the visible UI names such as `containerScene`, `decomposerScene`, `reflectorScene`, and the intervention sub-tabs.

## Container And Layout Semantics

- `v` and `a` nodes are storage containers, not fixed scalar-type declarations.
- Expanded `v/a` nodes may contain internal layout fields.
- Internal layout fields are stored as `layoutFields` with:
  - `kind`
  - `bitWidth`
  - `ruleText`
- `ruleText` is a free-form contract between the UI and the current algorithm. It does not force a universal float/int/bool meaning.
- Good examples:
  - `from v1 to phase01 32`
  - `from a1 to x,y 16,16`
  - `from a2 to alive,team,heat 1,7,24`
- When a tutorial needs finer structure, explicitly teach the user to expand the container and refine it with layout fields.
- Resource nodes are not part of the default teaching documents. Only introduce them when the user explicitly requests a resource-driven workflow.

## Command Expectations

- Prefer direct `interface4agents` commands over abstract descriptions when a command exists.
- Use `highlight field <container>` when you need to point at the layout-rule area inside an expanded container.
- Use `field ...` commands when you need to refine a container into smaller parts.
- Do not describe hidden implementation details as if they were user-visible UI.

## Build Explanation

When the user asks how build works, explain it at a high level:

- the editor materializes package data and generated source assets into the algorithm output folder
- stage shader files are written from the current intervention setup
- generated plugin or script support files are emitted as needed
- the repository build batch is then invoked for the target algorithm

Keep the explanation aligned with the current editor state instead of older document-only workflows.
