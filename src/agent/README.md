# Agent Composition

This module owns the agent object that composes algorithm packages, resource bindings, and routing metadata.

## Responsibilities

- Agent initialization and destruction.
- Agent-owned bound resources such as `mesh`, descriptor-shaped runtime data, and other resources needed by mounted packages.
- Ordered multi-package container descriptors.
- Holding ordinary packages such as the camera package for higher-level composition.
- Decomposer and reflector hooks are retained as agent-level capabilities.
- Optional intervention packages may also carry UI, signal protocol, decomposition, and reflection behavior when present.
- Solver metadata and mounted-agent metadata that must travel across runtime boundaries.
- Agent-owned pipeline descriptors that define package order and container routes.

## Agent Role

This module is not a runtime tick loop.

It acts as a cross-boundary carrier between algorithm packages, codec payloads, and the higher-level runtime code. A single agent can represent a pipeline made of multiple algorithm packages, so render-side or simulation-side composition can stay in one agent instead of being split into many hard-coded wrappers.

## Agent Summary

- The agent owns the agent boundary, not the algorithm semantics.
- `bound_resources` is the agent-visible resource set, and `mesh` belongs here as one of the bound resources.
- `compliance_packages` hold the package codecs for ordinary algorithms.
- Decomposition and reflection are agent-level capabilities that the agent retains and exposes.
- An optional intervention package can sit on top of the same agent and provide UI, signal handling, and intervention delegation.
- If an agent does not mount an intervention package, it can still run ordinary packages through its bound resources and package codec behavior.

## Construction Rules

- `AgentPipelineDescriptor::ordered_bindings` defines the execution order.
- `AgentPipelineDescriptor::container_routes` defines how one package's containers feed the next package.
- `AgentPipelineDescriptor::component_descriptors` stores the lower-level descriptors the agent is built from.
- Package-scoped aliases live in the final composite container descriptor and let the same source container name map differently per package.
- The runtime does not validate the wiring graph. Missing or incompatible bindings are expected to fail at the point of use.

## Helper Docs

- `pipeline_manifest_template.md` shows how to assemble a multi-algorithm agent.
- `tools/algorithm_descriptor_composer/descriptor_composer.py` is a GUI helper for composing and renaming container descriptors.
- The template is intentionally data-first so it can be mirrored in code, JSON, or a future script.
