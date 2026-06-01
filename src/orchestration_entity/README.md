# Orchestration Entity Layer

This folder hosts orchestration entities that organize multi-layer algorithm work.

## Responsibilities

- Orchestration entity initialization and destruction.
- Ordered multi-package compliance containers.
- Holding ordinary packages such as the camera package for higher-level orchestration.
- Intervention, decomposer, and reflector hooks for higher-level algorithms.
- Solver metadata and mounted-agent metadata that must travel across layer boundaries.
- Instance-owned pipeline descriptors that define package order and container routes.

## Layer Role

`orchestration_entity` is not a leaf runtime layer.

It acts as a cross-layer carrier between algorithm packages, codec payloads, and higher-level orchestration/runtime code. A single entity can now represent a pipeline made of multiple algorithm packages, so render-side or simulation-side composition can stay in one entity instead of being split into many hard-coded wrappers.

## Construction Rules

- `OrchestrationEntityPipelineDescriptor::ordered_bindings` defines the execution order.
- `OrchestrationEntityPipelineDescriptor::container_routes` defines how one package's containers feed the next package.
- `OrchestrationEntityPipelineDescriptor::component_descriptors` stores the lower-level compliance descriptors the entity is built from.
- Package-scoped aliases live in the final composite compliance descriptor and let the same source container name map differently per package.
- The runtime does not validate the wiring graph. Missing or incompatible bindings are expected to fail at the point of use.

## Helper Docs

- `pipeline_manifest_template.md` shows how to assemble a multi-algorithm entity.
- `tools/algorithm_descriptor_composer/descriptor_composer.py` is a GUI helper for composing and renaming descriptors.
- The template is intentionally data-first so it can be mirrored in code, JSON, or a future script.
