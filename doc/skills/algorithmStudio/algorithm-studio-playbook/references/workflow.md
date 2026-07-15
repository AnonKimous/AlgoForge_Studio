# Workflow

This skill is a reusable operating contract for Algorithm Studio.

## 1. Authoring

- Start in `algorithmDevScene`.
- Create the minimum algorithm containers and functions needed for the demo.
- Use `helperScene` only for aliasing, container organization, decomposer work, or d2c layout.
- Keep phase-specific logic in the phase tabs instead of scattering it across unrelated scenes.

## 2. Internal Agent Commands

Use the native command families:

- `scene`
- `v`
- `function`
- `phase`
- `highlight`

These commands match the UI model and keep the agent focused on actual editor actions.

## 3. Self-Build

When the internal agent is satisfied with the algorithm:

1. save the project state
2. run the repository build script
3. open `debugTool`
4. inspect the render preview result

Do not let the agent invent a separate build path unless the repo scripts are missing.

## 4. Feedback Output

When producing feedback for the user, report:

- what was changed
- what scene or phase was involved
- what build or preview step is next
- what still needs manual inspection

## 5. Render Preview

`renderpreview` is for observation, not authoring.

Use it to:

- launch `debugTool`
- inspect the latest preview artifact
- confirm the algorithm behaves as expected after build
