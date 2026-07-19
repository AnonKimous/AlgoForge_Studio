#version 450

layout(set = 0, binding = 0) readonly buffer VertexData {
  float data[];
} vertex_data;
layout(location = 0) flat out float body_id;

void main() {
  uint base = uint(gl_InstanceIndex) * 7u;
  vec4 position_data = vec4(
    vertex_data.data[base + 0u],
    vertex_data.data[base + 1u],
    vertex_data.data[base + 2u],
    vertex_data.data[base + 3u]);
  float scale_x = vertex_data.data[base + 4u];
  float scale_y = vertex_data.data[base + 5u];
  float angle_z = vertex_data.data[base + 6u];
  const vec2 offsets[4] = vec2[4](
    vec2(-1.0, -1.0),
    vec2(1.0, -1.0),
    vec2(-1.0, 1.0),
    vec2(1.0, 1.0));
  vec2 local_point = offsets[gl_VertexIndex] * position_data.z * vec2(scale_x, scale_y);
  float sine = sin(angle_z);
  float cosine = cos(angle_z);
  vec2 rotated_point = vec2(
    local_point.x * cosine - local_point.y * sine,
    local_point.x * sine + local_point.y * cosine);
  vec2 point = vec2(position_data.x, position_data.y) + rotated_point;
  gl_Position = vec4(point.x / 6.0, (point.y - 2.5) / 3.75, 0.0, 1.0);
  body_id = position_data.w;
}
