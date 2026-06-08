#version 450

// Result-render stage shader for the temporary test algorithm.
// Expected input layout:
// - set=0, binding=0
// - a tightly packed float array
// - every point uses 3 floats: x, y, z
// - the first point is read from indices [0..2]
//
// The position is treated as already being in clip/NDC space.

layout(set = 0, binding = 0) readonly buffer ResultRenderPositions {
  float data[];
} positions;

layout(location = 0) out vec2 v_uv;

void main() {
  const vec2 quad_offsets[4] = vec2[4](
    vec2(-1.0, -1.0),
    vec2(1.0, -1.0),
    vec2(-1.0, 1.0),
    vec2(1.0, 1.0)
  );

  uint base_index = uint(gl_InstanceIndex) * 3u;
  vec3 pos = vec3(
    positions.data[base_index + 0u],
    positions.data[base_index + 1u],
    positions.data[base_index + 2u]
  );

  vec2 corner = quad_offsets[gl_VertexIndex];
  vec2 radius = vec2(0.04, 0.04);
  gl_Position = vec4(pos.xy + corner * radius, pos.z, 1.0);
  v_uv = corner;
}
