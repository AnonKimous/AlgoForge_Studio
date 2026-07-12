# AGENTS.md

## General Constraints

- Do not add defensive code unless the user explicitly allows it.
- Do not add null checks, fallback branches, or recovery paths intended to make unexpected states execute legally. Unexpected states should fail directly.
- If compilation fails because of the machine PATH environment, try using `Path` instead of `PATH`. If the repository contains a `build_local` file, it may be used for compilation.
- Never modify third-party library source code. If a third-party library must change, replace the complete library instead; do not patch it in place.
- Do not modify third-party code under any circumstances.

## Project Constraints

- Do not modify `CMakeLists.txt` unless the user explicitly approves that change in the current turn.
- Keep the call chain aligned with `sdk -> agent_management -> agent -> algorithm_management -> runtime_systems`.
- Build algorithms through the repository's dedicated batch tools. Do not modify build scripts to work around an algorithm build.
- Preserve the existing file encoding. This repository uses UTF-8 for source files, comments, UI text, and build output.
- Do not modify third-party libraries.

## Architecture Guardrails

- The mainline only provides the algorithm execution environment.
- The mainline must not own algorithm semantics, container meaning, object counts, particle counts, or rendering counts.
- The mainline must not read an algorithm's explicit count field, infer a draw count from container sizes, or derive algorithm behavior from array lengths.
- Algorithms own their data, object counts, particle counts, fireworks counts, and rendering submissions.
- Keep bridge rules separate from mapping rules. A bridge is not a mapping table.
- Do not copy complete algorithm containers through a bridge when the standard container slot can be used directly.
- Keep the call chain aligned with `sdk -> agent_management -> agent -> algorithm_management -> runtime_systems`.

## Coordinate Convention

- Use a left-handed coordinate system.
- The origin `[0,0,0]` is the lower-left near corner.
- `+X` points right, `+Y` points up, and `+Z` points into the screen.
- Vulkan viewport setup in this repository is flipped to match this convention.
- Render Preview shaders interpret `x` and `y` as preview-page pixel coordinates.
- In Render Preview, `[0,0]` is the lower-left corner of the ImGui content region below the title bar.
- For intervention and result-render stage logic, use GLSL shader code by default unless the user explicitly requests another implementation form.

## Runtime Lifetime

- Once an algorithm container is submitted, keep it active while the algorithm remains mounted.
- Do not unload an algorithm merely because one execution phase has completed.
- Algorithms with an explicit execution-end condition may finish; otherwise mounted algorithms remain active.

## Test and Artifact Locations

- Redirect all test outputs and transient test artifacts into `testData\`.
- Put new temporary test data under `testData\` and keep the repository root clean.
- Pipeline timing artifacts may use the repository's existing `artifacts\pipeline_timing\` directory.
- Do not create transient runner logs, images, or reports at the repository root.

## Runner Completion Validation

- After completing any change that can affect an algorithm, pipeline, runtime, Vulkan execution, preview rendering, or debugTool behavior, run the repository runner before reporting completion.
- Run the runner only after the final build succeeds; a successful compile without runner validation is not completion.
- Use the workflow in `skills/runner-validation/SKILL.md`.
- Start `build\Debug\debugTool.exe --runner-server --runner-server-once --runner-endpoint 127.0.0.1:0`, then run the matching `--algorithm-runner` or `--pipeline-runner` client.
- A client that only prints `runner_client.begin` is not a validation result.
- Require `OK algorithm_runner` or `OK pipeline_runner`.
- Inspect the latest runner log and timing CSV.
- For drawable preview work, require nonzero preview pixels and ready pipeline and target state. `pipeline=not_ready`, `target=not_ready`, or `preview_pixels=0` is a failure.
- A timing CSV with completed stage rows proves only that the measured tick completed; it does not prove that preview rendering succeeded.

## Visual Studio Launching

- When launching `debugTool` from Visual Studio, keep the debugger working directory at the repository root, for example `$(SolutionDir)..\`.
- Relative resources such as `data\teapot.obj` will not resolve correctly when the debugger working directory is wrong.

## Completion Reporting

- Report the exact build command and runner command used for validation.
- Report whether the runner returned `OK`.
- Report the newest pipeline timing result when a pipeline was involved.
- If validation fails, do not report the task as complete. State the failing log line or artifact path.
- When preview behavior is in scope, verify that the runner can export a preview image and that the log reports nonzero preview pixels.

## Project Skills

Read the relevant English skill before changing the corresponding subsystem:

- `debugtoolSkill\scheduler-runtime\SKILL.md` for scheduler registration, pipeline stages, lanes, runtime state, and tick flow.
- `debugtoolSkill\decomposer-manifest\SKILL.md` for package manifests, decomposition, runtime mappings, wrapper stages, and bridge ingress/egress.
- `debugtoolSkill\debugtool-cli-runner\SKILL.md` for CLI connection, runner commands, preview export, and timing validation.
- `debugtoolSkill\windows-path-environment\SKILL.md` for the main development machine's broken PATH environment and build-tool initialization.
- `debugtoolSkill\algorithm-development\SKILL.md` for the algorithm catalog, package files, development documents, build paths, mounting, protocol, and performance validation.

The Python runner facade is documented in `aglopy\README.md`. Use it for repeatable algorithm and pipeline checks from Python scripts.

All skill files under `debugtoolSkill` are English-only. Keep future additions to these skills in English.
