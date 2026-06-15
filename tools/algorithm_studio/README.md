# Algorithm Studio

This tool is a local Python UI for authoring algorithm packages.

It provides:

- a drag palette for inserting containers and blocks
- a canvas for positioning nodes
- package JSON export

## Run

```bat
tools\launch_algorithm_studio.bat
```

## Current scope

This is a UI-first prototype.

- The editor can import and export the repository's package JSON shape.
- The project can be extended later to generate C++ and trigger hot builds.

## Launch behavior

- The launcher is implemented directly in `tools\launch_algorithm_studio.bat`.
- It prefers the Anaconda `pytorch` environment when `conda.bat` is available.
- If `tools\algorithm_studio\requirements.txt` is present and non-empty, it installs those packages automatically before launch.
