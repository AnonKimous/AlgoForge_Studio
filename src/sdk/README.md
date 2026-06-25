# SDK Surface

This directory is the external agent and algorithm submission surface.

## What It Exposes

- `SdkRuntimeSystem`
- create agent
- destroy agent
- mount algorithm to an agent
- mount resources onto a draft algorithm
- unmount a draft or submitted algorithm
- submit a draft algorithm
- decomposer creation and request/binding helper types
- agent creation accepts a `limit_fps_flag`:
  - the default value comes from `config/kernelCfg.json`
  - `120` means the agent will be ticked at up to 120 Hz
  - `0` means the agent is ticked once and then held until reconfigured

## What It Does Not Expose

- UI code
- runtime windowing
- reflector UI
- intervention UI
- reflector construction or reflector runtime access
- intervention package creation or intervention UI hooks

## Debug Boundary

The external SDK is submission-only. Reflectors and intervention packages are
reserved for `debugTool` and other internal debug-time tooling, not for SDK
consumers.

## Descriptor Boundary

- SDK descriptor submission is value-oriented. Container declarations are storage-only and do not expose a semantic scalar type.
- Container bit width such as `32` or `64` should not be interpreted by SDK users as meaning `int32`, `uint32`, `float32`, or `float64`.
- Package-side decomposer rules decide how descriptor values are encoded into container bytes.
