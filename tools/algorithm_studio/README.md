# Algorithm Studio

This tool is a local Python UI for authoring algorithm packages.

It provides:

- a drag palette for inserting `Container`, `ToolNodes`, and `ResNode`
- a canvas for positioning nodes and drawing `container` grouping boxes
- a collapsible right-side `ChatBox` that can talk to either a provided API or the local `codex` CLI
- a chat approval selector with `manual` and `rules` modes backed by `accessRules.md`
- package JSON export
- `Container` includes `Variable` and `Array`
- `container` boxes can wrap `Variable` and `Array` nodes into a named merged alias
- standard-slot aliases such as `a1:vertex` or `v1,v2:pos` are allowed as authoring semantics
- `ToolNodes` includes `container`, `decomposer`, `reflector`, and `interventioner`
- `container` is storage-only metadata; it does not by itself mean `float`, `int`, `bool`, `fp8x4`, or another scalar interpretation
- `container` owns byte storage, grouping, and transfer shape, but it does not own byte-semantics interpretation
- front-end text, labels, and teaching content may describe how a container's bytes are expected to be used, but those descriptions are guidance for humans and UI, not the container's runtime meaning
- the actual read rule for those bytes belongs to the algorithm side that consumes them; for example, a stage may choose to read the same 32 bits as `float32`, `int32`, `fp8x4`, or `boolx32`
- if descriptor bytes need a non-default interpretation, that rule must be expressed by the consuming algorithm logic rather than silently assumed from the container node alone
- `ResNode` palette inserts a single `mesh` node; `decomposer.res.mesh` can expand to `edge`, `vertex`, and `normal`
- double-click `Variable` / `Array` nodes to edit temporary values
- double-click a node title bar, meaning the colored header area, to collapse or expand the node
- double-clicking the node body should not be described as the generic collapse gesture
- for most node kinds, a body double-click is treated as a zoom-style interaction instead of a collapse action
- `fun` nodes are special: double-clicking the node body opens the script instead of collapsing the node
- treat mouse-wheel on the scene canvas as canvas zoom; do not describe it as the normal way to move the canvas
- the internal agent now talks through `agents.py` and `interface4agents.py`
- `interface4agents` uses command scripts with one `cmd arg arg arg` line per command
- `highlight` flashes the UI location for a command, such as `highlight v add` or `highlight scene container`
- the first command set covers `v`, `a`, `scene`, `createCosNode`, `hang`, `integrateChild`, and `hotReview`
- `createCosNode cosNode` creates a nested `containerElement`
- `hang v1 cosNode` inserts `v1` into `cosNode`
- `integrateChild cosNode` repacks the `cosNode` tree after insertion; `intergrateChild` is accepted as an alias
- `hotReview` jumps the canvas view to the first visible node or the current selection
- palette drags now follow the mouse on the canvas until release
- dragging a node above the canvas turns on a red waste-area warning and deletes the node on release
- right-dragging a container duplicates it, keeps its custom data, and draws a dashed reuse line between copies

## Run

Double-click `tools\launch_algorithm_studio.bat`, or run the same command from a terminal:

```bat
py -3 tools\algorithm_studio\algorithm_studio.py
```

If you prefer the script path form, this also works:

```bat
py -3 algorithm_studio.py
```

## Current scope

This is a UI-first prototype.

- The editor can import and export the repository's package JSON shape.
- Left-drag blank canvas space to box-select containers into a new `container`.
- Drag a node's header to move the node, drag its resize handle to stretch it.
- Right-drag blank canvas space to pan the scene.
- Mouse-wheel is for zooming the scene canvas, not for panning it.
- Double-click the colored node header to collapse or expand a node.
- Do not use “double-click the whole node to collapse” as the generic instruction.
- `fun` nodes are special: double-clicking the node body opens the script view.
- Drag from either an input port or an output port to create a connection.
- `accessRules.md` in `tools\` controls rule-based chat approval.
- The project can be extended later to generate C++ and trigger hot builds.
- The launcher expects Python 3.10 or newer.
- The agent command layer is intentionally separate from raw script editing.

## Pipeline Bridge Notes

- Pipeline algorithms should prefer `standard container` layouts.
- Pipeline and normal algorithms are handled by separate backend submission paths.
- The runtime bridge should resolve `standard_layout` first and treat standard slots as the primary stage-to-stage carrier.
- Non-standard containers must be transferred as same-name containers with identical structure, or the backend should fail fast.
- Standard slots like `v1`, `v2`, `v3` may be grouped into higher-level aliases for readability, while the runtime still resolves the underlying standard slots.
- Tool-side aliases are for authoring and debug readability only; exported runtime-facing package fields should still be written back as `vN/aN`.
- Direct transfer for non-standard containers must keep the same container name on both sides and the same structure.

## Container And Decode Semantics

- A `container` is still the carrier of bytes, layout grouping, and transfer identity.
- A `container` is not the authority for what those bytes "mean" numerically.
- Front-end copy can explain expected usage, such as "this buffer is usually read as `float32 position`" or "these 32 bits are often treated as `boolx32`", so users and agents know how to operate the graph.
- Expanded `v/a` nodes may also show front-end layout-rule lines such as `from v1 to x,y 16,16`; these are editable UI and algorithm hints rather than container-owned byte semantics.
- The algorithm implementation is the authority that decides how to decode and consume the bytes at runtime.
- That means the same stored bytes may be interpreted differently by different algorithm stages, as long as the consuming stage makes that rule explicit.
- If a stage requires a specific decode rule, that requirement should be documented in the node's algorithm/front-end description and enforced by the algorithm path, not hidden inside the container's base storage identity.

## Launch behavior

- The GUI runs on the machine's local Python interpreter.
- The Windows double-click launcher is `tools\launch_algorithm_studio.bat`.
- The direct script entry point is the stable cross-device launch path.
- `requirements.txt` is currently empty, so no automatic package install step runs before launch.
