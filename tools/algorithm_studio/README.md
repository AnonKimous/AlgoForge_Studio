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
- `ToolNodes` includes `container`, `decomposer`, `reflector`, and `interventioner`
- `ResNode` palette inserts a single `mesh` node; `decomposer.res.mesh` can expand to `edge`, `vertex`, and `normal`
- double-click `Variable` / `Array` nodes to edit temporary values
- double-click `Function` nodes to draft plan/code with the right-side model and write the result back into the function body
- mouse-wheel zoom scales the canvas and node text together; wheel over an `Array` node slides its preview window

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
- Drag from either an input port or an output port to create a connection.
- `accessRules.md` in `tools\` controls rule-based chat approval.
- The project can be extended later to generate C++ and trigger hot builds.
- The launcher expects Python 3.10 or newer.

## Launch behavior

- The GUI runs on the machine's local Python interpreter.
- The Windows double-click launcher is `tools\launch_algorithm_studio.bat`.
- The direct script entry point is the stable cross-device launch path.
- `requirements.txt` is currently empty, so no automatic package install step runs before launch.
