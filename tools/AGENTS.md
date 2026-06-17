# Algorithm Studio Agent Instructions

## Operating Modes

The agent operates in two explicit modes.

- `Plan mode`
- `Execution mode`

Treat mode selection as strict.

### Plan Mode

Enter this mode when the prompt says any of these:

- `Phase: planning only.`
- `plan mode`
- `规划模式`
- the prompt explicitly asks for planning, decomposition, or a proposal before edits

In plan mode:

- Think and plan first. Do not edit the scene yet.
- Do not emit any `algorithm-studio-tool` block.
- Do not rewrite the full `Document`.
- Produce a short working document that can directly guide later execution.
- Prefer 3 to 6 concrete steps.
- State the minimum required nodes, stages, files, and bindings.
- Explicitly call out what must not be added.
- If the current context or document is too long, compress it before continuing.

The plan-mode output should be plain text and should usually contain:

- `Goal`
- `Constraints`
- `Minimal structure`
- `Steps`
- `Execution focus`

Use this exact template when possible:

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
- the immediate next action for execution mode
```

Keep the plan document short. Prefer a working note that execution mode can act on immediately, not a long essay.

When the document or prior context is long:

- compress old conversation into a short working summary
- compress the current scene into only the structures relevant to the task
- compress the document into the minimum facts needed for execution
- carry forward only constraints, target shape, unresolved decisions, and the next step

### Execution Mode

Enter this mode when the prompt says any of these:

- `Phase: execution.`
- `execution mode`
- `执行模式`
- the prompt explicitly says to perform edits now

In execution mode:

- Act quickly and decisively.
- Read the latest plan document first if one exists.
- Treat that plan as the contract for what to do now.
- Reuse the `Steps` and `Execution focus` sections as the immediate execution checklist.
- Prefer the smallest coherent batch of edits.
- Prefer 1 to 4 precise tool calls over broad rewrites.
- Prefer UI tools first.
- Use `update_document` only when the requested structure is safer to express as one coherent document update.
- After tool calls, briefly state what changed and what remains.

### Plan To Execution Handoff

The output of plan mode is the instruction document for execution mode.

Execution mode should reuse the latest plan document and should not re-open the problem from scratch unless:

- the plan is invalid
- the scene changed in a way that breaks the plan
- execution reveals a hard constraint that the plan missed

If that happens:

- compress the new situation
- revise the plan briefly
- then continue execution

### Compression Rule

Whenever context becomes long, the agent should compress:

- old conversation
- scene state
- document state
- prior planning text

Compression must preserve:

- the user goal
- the current mode
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

## Document Editing Fallback

- `Document` remains the source of truth, but full document rewriting is a fallback path.
- If you must use `update_document`, always start from the current full `Document` JSON and return a full updated JSON object.
- The returned `document` must be valid JSON with a JSON object at the root.
- Do not return a patch, diff, comments, or placeholder schema.
- If a rename changes references, update every affected reference consistently.
- Preserve ordering relationships in the document when editing graph content.
- If the requested edit cannot be applied safely, explain why instead of emitting an invalid tool call.
