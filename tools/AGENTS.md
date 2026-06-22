# Algorithm Studio Agent Instructions

## Task Complexity

- First check whether the user command is short and whether there is a direct matching interface or tool path for it.
- If the command is short and there is a direct matching interface or tool path, handle it directly without first writing a plan or checklist.
- If the task is complex, ambiguous, risky, spans multiple coordinated edits, or does not have a clear direct interface path, prefer first producing a short working document or checklist instead of trying to handle everything immediately.
- Do not split a normal request into a mandatory plan phase and a later execution phase.
- If a checklist is genuinely needed, keep it inside the same pass and continue into execution instead of forcing a two-stage round trip.
- That working document should stay short and execution-oriented.
- Prefer 3 to 6 concrete steps.
- State the minimum required nodes, stages, files, and bindings.
- Explicitly call out what must not be added.
- Do not emit any `algorithm-studio-tool` block when only drafting the working document.
- Do not rewrite the full `Document` just to think through the task.

Use this template when a checklist or working document is needed:

```text
Goal
- one short statement of the task

Constraints
- hard constraint 1
- hard constraint 2

Minimal structure
- required node / stage / file 1
- required node / stage / file 2
- explicit "do not add" items if important

Steps
1. first execution step
2. second execution step
3. third execution step

Execution focus
- the immediate next action
```

When the document or prior context is genuinely long:

- compress old conversation into a short working summary
- compress the current scene into only the structures relevant to the task
- compress the document into the minimum facts needed for execution
- carry forward only constraints, target shape, unresolved decisions, and the next step
- do not repeatedly recompress unchanged context across consecutive turns

### Compression Rule

Whenever context becomes long enough to interfere with execution quality or prompt budget, the agent should compress:

- old conversation
- scene state
- document state
- prior working text

Do not compress on every turn.
Do not recompress unchanged skill instructions or unchanged AGENTS instructions unless they are part of the overlong context that must be reduced.

Compression must preserve:

- the user goal
- hard constraints
- the minimal target structure
- completed steps
- remaining steps
- unresolved risks
- the immediate next action

## Preferred Editing Strategy

- Prefer UI tools first. Use them for adding, updating, or deleting nodes and rules.
- Only use `update_document` when the user explicitly asks to edit raw document text, or when the requested change cannot be expressed safely with the UI tools.
- Apply the smallest possible change that satisfies the user request.
- Keep unrelated fields unchanged.
- Do not invent extra nodes, resource nodes, stages, functions, reflectors, rules, or scaffolding unless the user explicitly asked for them.
- In Chinese requests, bare `容器` means `containerElement` by default, not a variable node.
- Prefer `kind: "container"` for that case.
- Use `variable` only when the user explicitly asks for `v节点`, `变量节点`, or names like `v1`.
- Use `array` only when the user explicitly asks for `a节点`, `数组节点`, or names like `a1`.
- `meshNode` should stay minimal and only represent `[mesh]`.
- The singleton tool-like nodes should be reused instead of duplicated when possible: `container`, `decomposer`, `reflector`, `interventioner`, `meshNode`, `fun`.
- For `fun`, keep two layers:
  - the `fun` node itself stores real code such as C++ / shader content
  - a linked `functiontext` node stores editable natural-language solution text
- If the user asks for方案/思路/解法 around `fun`, prefer `functiontext`.
- If the user asks for真实函数/真实shader/C++代码, prefer updating the `fun` node script.
- For minimal demo algorithms, especially descriptor-driven single-point motion demos, read `tools/skills/algorithm-studio-minimal-demo/SKILL.md` first and follow its scope rules before editing the scene.

### Canvas Interaction Notes

- When describing Algorithm Studio interactions, treat the colored node header as the node title bar.
- The generic collapse / expand gesture is double-clicking that colored node header, not double-clicking anywhere on the node body.
- Do not describe mouse-wheel as the normal way to pan the scene canvas; for agent guidance, mouse-wheel should be treated as canvas zoom.
- Do not describe “double-click the whole node to collapse” as a universal rule.
- `fun` nodes are special: double-clicking the node body opens the script instead of collapsing the node.

### Standard Slot Alias Rule

- Standard-slot aliases are tool-side semantics only.
- The scene or working document may describe aliases such as `a1:vertex` or `v1,v2:pos` for readability.
- Single-slot aliases still follow the underlying `v` or `a` slot rules.
- Multi-slot aliases may be shown as higher-level grouped variables in `containerElement` details.
- When emitting or editing package fields that are consumed by the current trunk runtime, always write the underlying `vN/aN` names instead of alias names.

### Pipeline Mapping Rule

- When preparing pipeline runtime mapping data, do not assume every stage exposes an identical standard container.
- The tool should think in terms of:
  - one shared compatible `v/a` prefix across all stages
  - plus stage-local extra standard slots
- Those extra standard slots must be summarized into the pipeline mapping metadata with cumulative offsets, so later bridge code can treat them as registered stage-local extensions instead of unnamed loose slots.
- The current runtime is intentionally stricter: extra standard slots may only be extra `v` registers.
- If a stage declares any extra standard `a` outside the shared prefix, the runtime should fail fast instead of trying to adapt it.
- The implicit pipeline `stageBuffer` must stay inside the shared `a` prefix, not inside a stage-local extra `a`.

## UI Tools

Use these exact formats:

```algorithm-studio-tool
{"tool":"ui_add_node","kind":"variable","name":"v1","count":1,"stride":4,"message":"Added v1"}
```

```algorithm-studio-tool
{"tool":"ui_update_node","kind":"variable","name":"v1","count":8,"message":"Updated v1 count"}
```

```algorithm-studio-tool
{"tool":"ui_add_node","kind":"functiontext","function_name":"fun","text":"solution draft","message":"Added function text"}
```

```algorithm-studio-tool
{"tool":"ui_update_node","kind":"functiontext","name":"fun_text","text":"updated solution draft","message":"Updated function text"}
```

```algorithm-studio-tool
{"tool":"ui_delete_node","kind":"variable","name":"v1","message":"Deleted v1"}
```

```algorithm-studio-tool
{"tool":"ui_add_rule","name":"v1_to_v2","source":"v1","target":"v2","map_kind":"v2v","message":"Added rule"}
```

Rules:

- `ui_add_node` kinds currently supported: `variable`, `array`, `stage`, `interventioner`, `resnode`, `function`, `functiontext`, `reflector`.
- `ui_add_node` also supports `container`, `containerelement`, `container_group`, `containergroup` for containerElement nodes.
- `ui_update_node` should be used to change existing node properties instead of rewriting the whole document.
- `ui_delete_node` should be used to remove an existing node by `kind` and `name`.
- `ui_add_rule` should be used to add a decomposer rule between existing containers.
- Use `functiontext` for detached editable solution text linked to `fun`.
- Use the `fun` node script for real code such as C++ and shader content.
- Prefer these tools over `update_document` whenever possible.

### Agent UI Commands

- When the task is about teaching the user to operate Algorithm Studio, prefer the `interface4agents` command script layer instead of raw document edits.
- Use `highlight` to point at the UI location of a command before explaining it.
- Start with the `v`, `a`, and `scene` command families.

## Document Editing Fallback

- `Document` remains the source of truth, but full document rewriting is a fallback path.
- If you must use `update_document`, always start from the current full `Document` JSON and return a full updated JSON object.
- The returned `document` must be valid JSON with a JSON object at the root.
- Do not return a patch, diff, comments, or placeholder schema.
- If a rename changes references, update every affected reference consistently.
- Preserve ordering relationships in the document when editing graph content.
- If the requested edit cannot be applied safely, explain why instead of emitting an invalid tool call.
