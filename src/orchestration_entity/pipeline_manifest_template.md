# Orchestration Entity Pipeline Template

This template shows how one entity can own a serial algorithm pipeline and redirect containers between steps.

## Example

Suppose a single render or simulation entity uses two algorithms:

- `rotate_vertices` computes a rotation from the shared mesh vertices and edges using its own quaternion state.
- `translate_vertices` applies a displacement matrix after the rotation step.

The entity can keep one shared geometry container and redirect the per-step state containers as needed.

```cpp
OrchestrationEntityInitConfig config{};
config.algorithm_name = "vertex_motion_pipeline";
config.mounted_agent_name = "physics_agent";
config.bound_resources = {"mesh", "physics_state", "compute_context"};

config.pipeline_descriptor.ordered_bindings = {
  {
    .package_name = "rotate_vertices",
    .input_containers = {"shared_vertices", "shared_edges", "rotation_quaternion"},
    .output_containers = {"rotated_vertices"},
    .container_aliases = {
      {.package_name = "rotate_vertices", .source_name = "vertexs", .alias_name = "vertexArray"},
      {.package_name = "rotate_vertices", .source_name = "rotated_vertices", .alias_name = "vertexArray"},
    },
  },
  {
    .package_name = "translate_vertices",
    .input_containers = {"rotated_vertices", "translation_matrix"},
    .output_containers = {"final_vertices"},
    .container_aliases = {
      {.package_name = "translate_vertices", .source_name = "vertexs", .alias_name = "vertexArray"},
      {.package_name = "translate_vertices", .source_name = "rotated_vertices", .alias_name = "vertexArray"},
    },
  },
};

config.pipeline_descriptor.container_routes = {
  {
    .source_package_name = "rotate_vertices",
    .source_container_name = "rotated_vertices",
    .target_package_name = "translate_vertices",
    .target_container_name = "rotated_vertices",
  },
};

config.pipeline_descriptor.component_descriptors = {
  rotate_vertices_descriptor,
  translate_vertices_descriptor,
};
```

## Practical Notes

- Use the lower-level container descriptor as the source of truth for each package.
- Build the entity-level descriptor by collecting the package descriptors, execution order, and container routes.
- The runtime intentionally avoids validating the graph. If a binding is wrong, the failure should surface when the algorithm consumes the container.
- Intervention packages can be attached per entity when UI, signal handling, or intervention delegation is needed; otherwise ordinary package codecs can stand on their own.
- Package aliases are package-scoped, so `rotate_vertices.vertexs` and `translate_vertices.vertexs` can both map to `vertexArray` without ambiguity.
