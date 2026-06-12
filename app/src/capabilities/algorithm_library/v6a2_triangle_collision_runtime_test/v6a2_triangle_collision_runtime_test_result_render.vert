#version 450

layout(set = 0, binding = 0) readonly buffer RenderPreviewData {
  float data[];
} render_data;

layout(location = 0) out vec2 v_uv;
layout(location = 1) flat out uint v_kind;
layout(location = 2) flat out vec3 v_color;

const vec2 kQuadOffsets[4] = vec2[4](
  vec2(-1.0, -1.0),
  vec2(1.0, -1.0),
  vec2(-1.0, 1.0),
  vec2(1.0, 1.0)
);

void EmitPoint(uint record_index, float radius, vec3 color) {
  uint base_index = record_index * 6u;
  vec3 pos = vec3(
    render_data.data[base_index + 0u],
    render_data.data[base_index + 1u],
    render_data.data[base_index + 2u]
  );

  vec2 corner = kQuadOffsets[gl_VertexIndex];
  gl_Position = vec4(pos.xy + corner * radius, pos.z, 1.0);
  v_uv = corner;
  v_kind = 0u;
  v_color = color;
}

void EmitEdge(uint edge_index, vec3 color) {
  uint base_index = (4u + edge_index) * 6u;
  vec3 start = vec3(
    render_data.data[base_index + 0u],
    render_data.data[base_index + 1u],
    render_data.data[base_index + 2u]
  );
  vec3 end = vec3(
    render_data.data[base_index + 3u],
    render_data.data[base_index + 4u],
    render_data.data[base_index + 5u]
  );

  vec2 delta = end.xy - start.xy;
  float length_sq = dot(delta, delta);
  vec2 tangent = length_sq > 1e-12 ? delta / sqrt(length_sq) : vec2(1.0, 0.0);
  vec2 normal = vec2(-tangent.y, tangent.x);
  float half_width = 0.0125;

  vec2 position = start.xy;
  vec2 uv = vec2(0.0, 1.0);
  if (gl_VertexIndex == 0u) {
    position = start.xy + normal * half_width;
    uv = vec2(0.0, 1.0);
  } else if (gl_VertexIndex == 1u) {
    position = start.xy - normal * half_width;
    uv = vec2(0.0, -1.0);
  } else if (gl_VertexIndex == 2u) {
    position = end.xy + normal * half_width;
    uv = vec2(1.0, 1.0);
  } else {
    position = end.xy - normal * half_width;
    uv = vec2(1.0, -1.0);
  }

  gl_Position = vec4(position, start.z, 1.0);
  v_uv = uv;
  v_kind = 1u;
  v_color = color;
}

void main() {
  uint instance_index = uint(gl_InstanceIndex);
  if (instance_index == 0u) {
    EmitPoint(0u, 0.085, vec3(1.0, 0.42, 0.24));
    return;
  }

  if (instance_index < 4u) {
    EmitPoint(instance_index, 0.055, vec3(1.0, 0.93, 0.34));
    return;
  }

  EmitEdge(instance_index - 4u, vec3(0.16, 0.82, 1.0));
}
