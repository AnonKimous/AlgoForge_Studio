#version 450

layout(set = 0, binding = 0) readonly buffer PositionXBuffer {
  float value[];
} position_x;

layout(set = 0, binding = 1) readonly buffer PositionYBuffer {
  float value[];
} position_y;

layout(push_constant) uniform PreviewViewport {
  float width;
  float height;
} preview_viewport;

layout(location = 0) out vec2 v_uv;

vec4 ToPreviewClip(vec2 preview_position) {
  vec2 ndc = vec2(
    (preview_position.x / preview_viewport.width) * 2.0 - 1.0,
    (preview_position.y / preview_viewport.height) * 2.0 - 1.0
  );
  return vec4(ndc, 0.0, 1.0);
}

void main() {
  const vec2 quad_offsets[4] = vec2[4](
    vec2(-1.0, -1.0),
    vec2(1.0, -1.0),
    vec2(-1.0, 1.0),
    vec2(1.0, 1.0)
  );

  const vec2 center = vec2(position_x.value[0], position_y.value[0]);
  const vec2 corner = quad_offsets[gl_VertexIndex];
  const float radius = 10.0;
  gl_Position = ToPreviewClip(center + corner * radius);
  v_uv = corner;
}
