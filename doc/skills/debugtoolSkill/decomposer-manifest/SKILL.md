---
name: decomposer-manifest
description: Work with the current algorithm package manifest schema, package loader, resource and descriptor decomposition, runtime mappings, wrapper stages, and bridge ingress/egress. Use when adding containers, changing manifest mappings, changing decomposer behavior, or debugging package mount and transfer failures.
---

# Decomposer and Manifest

Read the current source before editing a package schema:

- `src/algorithm_catalog/algorithm_package_loader.cpp`
- `src/algorithm_catalog/algorithm_package_decomposer.cpp`
- `src/algorithm_catalog/algorithm_protocol.h`
- `src/algorithm_catalog/algorithm_types.h`
- `algorithmLib\algorithmSrc\pipeline\v4a16_fireworks_pipeline_demo\manifest.json`
- One stage manifest under the same pipeline directory

## Manifest sections used by the current loader

The loader currently reads these areas:

- `algorithm_name`
- `container.variable`
- `container.variableArray`
- `container.aliases`
- `reflector`
- `runtime.launchOnce`
- `runtime.pipeline.supportsCircularTick`
- `runtime.pipeline.mappings`
- `runtime.pipeline.externalWriteResetContainers` or `clearAfterTickContainers`
- `exec.used_algorithm_containers`
- `exec.shader`
- `stage.resultRender`
- `wrapper.stage.stageBegin` and `wrapper.stage.stageEnd`

Container declarations define standard container slots. A mapping entry maps a source container name to a target container name across a named stage edge. The loader accepts mapping objects and mapping arrays; preserve the form already used by the package unless the parser is intentionally changed.

For GPU phases, `used_algorithm_containers` is positional. The current loader builds runtime VK bindings from manifest order and records container kind, tuple width, and required status. Shader descriptor bindings must stay aligned with that order. Adding an array before variables can shift the variable binding numbers in a paired exec shader; update the shader bindings and rebuild the package together.

## What the decomposer does

`PackageSchemaDecomposer` loads the package schema and delegates to:

- `PackageResourceDecomposer` for requested and materialized resources, including mesh-backed resources;
- `PackageDescriptorDecomposer` for descriptor bindings and reflected data;
- pipeline bridge helpers for stage ingress, egress, and debug captures.

The decomposer resolves container names, aliases, view aliases, description bindings, mesh fields, resource groups, packed scalar segments, and descriptor targets. It validates references against the declared standard containers and returns explicit errors for unknown or empty targets.

## Mapping versus bridge

Keep these concepts separate:

- A mapping describes how named containers correspond across a stage edge.
- A bridge applies the runtime transfer behavior at ingress or egress and can capture debug state.

The relevant functions are `PipelineStageBridgeIngress`, `PipelineStageBridgeEgress`, `PipelineStageBridgeCaptureIngressDebugSet`, and `PipelineStageBridgeCaptureEgressDebugSet`. Do not use a bridge as a reason to copy every algorithm container. Prefer the existing standard container slot and only transfer the data required by the declared runtime mechanism.

## Safe manifest workflow

When adding a shared pipeline container:

1. Add the container to every real stage that owns or receives it.
2. Add matching entries to each manifest's runtime mapping edge that carries it.
3. Keep wrapper-only stages free of containers they do not own.
4. Update exec or resultRender bindings and shader descriptor bindings together.
5. Rebuild with `python boot\booterNinjaClang.py <algorithm>`.
6. Repackage the runtime through the repository's existing runtime packaging workflow.
7. Run the runner and inspect the newest package/cache paths in `testData` logs.

Do not make the mainline infer an algorithm count from array length or introduce an `instance_count` concept into the core merely to satisfy a render shader. If a backend needs an indirect draw command, declare a standard algorithm-owned command container and bind it explicitly.
