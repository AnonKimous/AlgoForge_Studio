#version 450

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
  const vec2 preview_positions[4] = vec2[4](
    vec2(0.0, 0.0),
    vec2(preview_viewport.width, 0.0),
    vec2(0.0, preview_viewport.height),
    vec2(preview_viewport.width, preview_viewport.height)
  );
  const vec2 uv_values[4] = vec2[4](
    vec2(0.0, 0.0),
    vec2(1.0, 0.0),
    vec2(0.0, 1.0),
    vec2(1.0, 1.0)
  );

  gl_Position = ToPreviewClip(preview_positions[gl_VertexIndex]);
  v_uv = uv_values[gl_VertexIndex];
}
