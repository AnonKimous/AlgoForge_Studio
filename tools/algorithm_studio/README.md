# Algorithm Studio

This tool is a local Python UI for authoring algorithm packages.

It provides:

- a drag palette for inserting `ContainerElement` and `ToolNodes`
- a canvas for positioning nodes and drawing `containerElement` grouping boxes
- a collapsible right-side `ChatBox` that can talk to either a provided API or the local `codex` CLI
- a chat approval selector with `manual` and `rules` modes backed by `accessRules.md`
- package JSON export
- `ContainerElement` includes `Variable` and `Array`
- `containerElement` boxes can wrap `Variable` and `Array` nodes into a named merged alias
- `ToolNodes` includes `containerElement`, `decomposer`, `reflector`, `interventioner`, and `resNode`

## Run

Double-click `tools\launch_algorithm_studio.bat`, or run the same command from a terminal:

```bat
py -3.9 -m algorithm_studio.algorithm_studio
```

If you prefer the script path form, this also works:

```bat
py -3.9 tools\algorithm_studio\algorithm_studio.py
```

## Current scope

This is a UI-first prototype.

- The editor can import and export the repository's package JSON shape.
- Left-drag blank canvas space to box-select containers into a new `containerElement`.
- Drag a node's header to move the node, drag its resize handle to stretch it.
- Right-drag blank canvas space to pan the scene.
- Drag from either an input port or an output port to create a connection.
- `accessRules.md` in `tools\` controls rule-based chat approval.
- The project can be extended later to generate C++ and trigger hot builds.

## Launch behavior

- The GUI runs on the machine's local Python interpreter.
- The Windows double-click launcher is `tools\launch_algorithm_studio.bat`.
- The module entry point supports both `py -m algorithm_studio.algorithm_studio` and direct script execution.
- `requirements.txt` is currently empty, so no automatic package install step runs before launch.
