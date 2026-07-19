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

- `python boot\booterNinjaClang.py <algorithm>`
  - builds the generated algorithm package
- `python boot\booterMSVC.py`
  - builds the debug window application
- `build\Microsoft\RelWithDebInfo\debugTool.exe`
  - launches the debug window for preview observation; Algorithm Studio invokes it directly

Use the repository Python launchers and the direct debugTool executable instead of legacy batch wrappers.
