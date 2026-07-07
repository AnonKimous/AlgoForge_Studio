# Command Guide

## Scene Commands

- `scene algorithmDevScene`
  - enters the main algorithm authoring surface
- `scene helperScene`
  - enters helper-only work
- `scene renderpreview`
  - enters preview observation mode

## Algorithm Commands

- `v add`
  - creates algorithm containers
- `function add`
  - creates a function node
- `function update`
  - writes or revises function logic
- `phase add`
  - creates a phase node for a specific execution stage
  - current UI text may still spell the post-exec phase as `aftertick`

## Why These Commands Exist

- `scene` keeps authoring scope explicit
- `v` and `function` express the real algorithm payload
- `phase` separates execution semantics from helper structure
- `highlight` gives the user a precise visual cue before each guided action

## Build And Preview

- `build_algorithm.bat`
  - builds the generated algorithm package
- `build_debugtool.bat`
  - builds the debug window application
- `run_debugtool_renderpreview.bat`
  - launches the debug window for preview observation

Use scripts instead of ad hoc shell logic when the internal agent needs to build or launch the result.
