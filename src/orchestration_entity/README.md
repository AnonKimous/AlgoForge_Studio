# Orchestration Entity Layer

This folder hosts orchestration entities that organize multi-layer algorithm work.

## Responsibilities

- Orchestration entity initialization and destruction.
- Entity-owned bound resources such as `mesh`, descriptor-shaped runtime data, and other resources needed by mounted packages.
- Ordered multi-package container descriptors.
- Holding ordinary packages such as the camera package for higher-level orchestration.
- Decomposer and reflector hooks are retained as entity-level capabilities.
- Optional intervention packages may also carry UI, signal protocol, decomposition, and reflection behavior when present.
- Solver metadata and mounted-agent metadata that must travel across layer boundaries.
- Entity-owned pipeline descriptors that define package order and container routes.

## Layer Role

`orchestration_entity` is not a leaf runtime layer.

It acts as a cross-layer carrier between algorithm packages, codec payloads, and higher-level orchestration/runtime code. A single entity can represent a pipeline made of multiple algorithm packages, so render-side or simulation-side composition can stay in one entity instead of being split into many hard-coded wrappers.

## Entity Summary

- The entity owns the entity boundary, not the algorithm semantics.
- `bound_resources` is the entity-visible resource set, and `mesh` belongs here as one of the bound resources.
- `compliance_packages` hold the package codecs for ordinary algorithms.
- Decomposition and reflection are entity-level capabilities that the entity retains and exposes.
- An optional intervention package can sit on top of the same entity and provide UI, signal handling, and intervention delegation.
- If an entity does not mount an intervention package, it can still run ordinary packages through its bound resources and package codec behavior.

## Construction Rules

- `OrchestrationEntityPipelineDescriptor::ordered_bindings` defines the execution order.
- `OrchestrationEntityPipelineDescriptor::container_routes` defines how one package's containers feed the next package.
- `OrchestrationEntityPipelineDescriptor::component_descriptors` stores the lower-level descriptors the entity is built from.
- Package-scoped aliases live in the final composite container descriptor and let the same source container name map differently per package.
- The runtime does not validate the wiring graph. Missing or incompatible bindings are expected to fail at the point of use.

## Helper Docs

- `pipeline_manifest_template.md` shows how to assemble a multi-algorithm entity.
- `tools/algorithm_descriptor_composer/descriptor_composer.py` is a GUI helper for composing and renaming container descriptors.
- The template is intentionally data-first so it can be mirrored in code, JSON, or a future script.
