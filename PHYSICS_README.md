# Physics Module README

## Overview

The physics path is driven from the main loop. Each frame:

1. `PhysModeController` receives input and decides whether to edit directives or advance simulation.
2. `PhysicsSolver` validates directives and computes the next mesh state.
3. The renderer only visualizes the result and the directive queue.

The module uses `float` throughout and fixed-step simulation at `1 / 120.0f` seconds per step.

## State Files

The project uses `data/subdivided_triangle.mesh.meshts` as the physics state file.

- Base geometry lives in `.mesh`
- The `.meshts` file stores:
  - current vertex positions
  - per-triangle material coefficients in GPa

If the `.meshts` file is missing, the app generates an in-memory default state before the first physics solve, and also writes a state file when physics first starts.

## Material Coefficients

Each triangle carries its own material coefficient.

- Materials are edited in Edit Mode
- The value is stored on the triangle, not globally
- The value is only written to disk when the user saves

Default handling:

- If a triangle is missing a coefficient, it gets the mean of the system's existing coefficients
- If the whole system has no valid coefficient yet, the triangle is treated as rigid with a very large coefficient

This means harder materials become more restrictive in simulation and are more likely to trigger red / invalid state earlier.

## Simulation Loop

`PhysModeController::Tick(...)` is the entry point for physics mode.

### Run

- Accumulates frame time
- Steps the solver in `1 / 120.0f` second chunks
- Writes the updated vertex positions back to the mesh

### Pause

- Keeps the current state
- Does not advance the solver

### Stop

- Stops advancing the solver
- Keeps the current mesh and directives

### Guide

- Enable / Disable only controls directive editing and visualization
- Guide lines are drawn only when guidance is enabled

## Directive Queue

The directive queue is visible in ImGui.

- Clicking a directive in ImGui selects it
- Double-clicking a directive arrow in the viewport selects it too
- The selected directive is highlighted in the queue and drawn thicker in the viewport

## Boundary Handling

Physics currently uses a simple boundary check on the normalized viewport area.

- If a step would move geometry outside the boundary, that step is rejected
- This acts as a temporary stop at the boundary

## Edit Mode Material UI

When a triangle is selected in Edit Mode:

- The current material coefficient is shown
- The coefficient can be edited directly
- A preset table provides common materials from soft to hard
- A preset can be applied by clicking or by drag-and-drop

## Save Behavior

Saving writes the full `.meshts` state:

- vertex positions
- triangle material coefficients

Geometry edits and material edits stay in memory until save is requested.
