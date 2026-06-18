#version 450

layout(set = 0, binding = 0) readonly buffer TickPositionsIn {
  float data[];
} positions_in;

layout(set = 0, binding = 1) buffer TickPositionsOut {
  float data[];
} positions_out;

layout(push_constant) uniform AlgorithmViewport {
  float width;
  float height;
} algorithm_viewport;

layout(location = 0) out vec2 v_uv;

vec4 ToAlgorithmClip(vec3 algorithm_position) {
  vec2 ndc = vec2(
    (algorithm_position.x / algorithm_viewport.width) * 2.0 - 1.0,
    (algorithm_position.y / algorithm_viewport.height) * 2.0 - 1.0
  );
  return vec4(ndc, algorithm_position.z, 1.0);
}

void main() {
  const vec2 quad_offsets[4] = vec2[4](
    vec2(-1.0, -1.0),
    vec2(1.0, -1.0),
    vec2(-1.0, 1.0),
    vec2(1.0, 1.0)
  );

  uint base_index = uint(gl_InstanceIndex) * 3u;
  vec3 pos = vec3(
    positions_in.data[base_index + 0u],
    positions_in.data[base_index + 1u],
    positions_in.data[base_index + 2u]
  );

  if (gl_VertexIndex == 0u) {
    positions_out.data[base_index + 0u] = pos.x + 0.10;
    positions_out.data[base_index + 1u] = pos.y + 0.03;
    positions_out.data[base_index + 2u] = pos.z;
  }

  vec2 corner = quad_offsets[gl_VertexIndex];
  // Keep the demo marker large enough to notice during a live GPU tick.
  vec2 radius = vec2(12.0, 12.0);
  gl_Position = ToAlgorithmClip(vec3(pos.xy + corner * radius, pos.z));
  v_uv = corner;
}
