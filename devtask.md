# Dev Task: Algorithm package artifact pipeline

## Goal

Define the final packaging and build pipeline for one algorithm so the project stops treating the editor document as the final artifact.

## Current understanding

An algorithm should now have these authoring and build stages:

- `manifest`
- `dev document` from the editor
- `cpp`
- `shader`

The editor document is only an intermediate authoring form. After submission to the AI agent, the agent rewrites the document into implementation artifacts according to the project rules.

Some algorithm types need special conversion rules:

- `decomposer` needs its own decomposition rule set
- `reflector` needs its own reflection function mapping
- `interventioner` needs its own control-signal style handling

The final runtime payload is not the full authoring set. At runtime, the algorithm should only load:

- one algorithm manifest
- one DLL
- one SPV

## Required end state

1. The authoring document is kept as a working intermediate form only.
2. The agent can rewrite that document into `cpp` and shader-related outputs using the algorithm-specific rules.
3. The build result is packaged into a single final file with the extension `.arg`.
4. The SDK uses `loadArg()` to load the `.arg` package directly.
5. `debugTool` does not become the runtime loader for split algorithm files.

## Packaging target

The final package should contain exactly the runtime-facing trio:

- algorithm manifest
- compiled DLL
- compiled SPV

That trio should be bundled into `.arg`.

## Implementation intent

This is a design/task note only for now.

- Do not change code yet.
- Do not start implementation until the next work session.
- Keep the packaging and loading path separate from the current debug-oriented split-file flow.

## Follow-up work for tomorrow

1. Define the `.arg` file structure.
2. Define how the editor document maps into the agent rewrite step.
3. Define the special conversion rules for decomposer / reflector / interventioner.
4. Add the SDK-side `loadArg()` path.
5. Add the build/pack step that emits the final `.arg` artifact.
