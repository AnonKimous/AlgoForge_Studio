#version 450

layout(set = 0, binding = 0) readonly buffer ResultRenderPositions {
  float data[];
} positions;

layout(push_constant) uniform PreviewViewport {
  float width;
  float height;
} preview_viewport;

layout(location = 0) out vec2 v_uv;

vec4 ToPreviewClip(vec3 preview_position) {
  vec2 ndc = vec2(
    (preview_position.x / preview_viewport.width) * 2.0 - 1.0,
    (preview_position.y / preview_viewport.height) * 2.0 - 1.0
  );
  return vec4(ndc, preview_position.z, 1.0);
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
    positions.data[base_index + 0u],
    positions.data[base_index + 1u],
    positions.data[base_index + 2u]
  );

  vec2 corner = quad_offsets[gl_VertexIndex];
  vec2 radius = vec2(12.0, 12.0);
  gl_Position = ToPreviewClip(vec3(pos.xy + corner * radius, pos.z));
  v_uv = corner;
}
