#version 450

// Result-render stage shader for the temporary test algorithm.
// Expected input layout:
// - set=0, binding=0 : point_x
// - set=0, binding=1 : point_y
// - set=0, binding=2 : point_z
//
// The position is treated as preview-page coordinates in pixels.
// The preview page origin is the lower-left corner.

layout(set = 0, binding = 0) readonly buffer ResultRenderPointX {
  float data[];
} point_x_buffer;

layout(set = 0, binding = 1) readonly buffer ResultRenderPointY {
  float data[];
} point_y_buffer;

layout(set = 0, binding = 2) readonly buffer ResultRenderPointZ {
  float data[];
} point_z_buffer;

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

  vec3 pos = vec3(
    point_x_buffer.data[uint(gl_InstanceIndex)],
    point_y_buffer.data[uint(gl_InstanceIndex)],
    point_z_buffer.data[uint(gl_InstanceIndex)]
  );

  vec2 corner = quad_offsets[gl_VertexIndex];
  vec2 radius = vec2(12.0, 12.0);
  gl_Position = ToPreviewClip(vec3(pos.xy + corner * radius, pos.z));
  v_uv = corner;
}
