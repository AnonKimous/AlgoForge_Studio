#version 450

layout(set = 0, binding = 0) readonly buffer SceneData {
  float data[];
} scene_data;

layout(set = 0, binding = 1) readonly buffer RenderDraw {
  uint data[];
} render_draw;

layout(push_constant) uniform PreviewViewport {
  float width;
  float height;
} preview_viewport;

layout(location = 0) out vec3 vertex_color;

vec4 PixelToClip(vec2 pixel) {
  return vec4(
    pixel.x / preview_viewport.width * 2.0 - 1.0,
    pixel.y / preview_viewport.height * 2.0 - 1.0,
    0.0,
    1.0
  );
}

void main() {
  const vec2 quad[4] = vec2[4](
    vec2(-0.5, -0.5),
    vec2(0.5, -0.5),
    vec2(-0.5, 0.5),
    vec2(0.5, 0.5)
  );
  const float pi = 3.14159265358979323846;
  const uint instance_index = uint(gl_InstanceIndex);
  const vec2 corner = quad[gl_VertexIndex];
  vec2 pixel;

  if (instance_index == 0u) {
    float angle = scene_data.data[2] * pi / 180.0;
    vec2 axis = vec2(cos(angle), sin(angle));
    vec2 normal = vec2(-axis.y, axis.x);
    vec2 local = vec2(corner.x * scene_data.data[4], corner.y * 14.0);
    pixel = vec2(scene_data.data[0], scene_data.data[1]) + axis * local.x + normal * local.y;
    vertex_color = scene_data.data[9] > 0.5 ? vec3(0.75, 0.15, 0.05) : vec3(0.65, 0.38, 0.12);
  } else {
    pixel = vec2(
      scene_data.data[5] + scene_data.data[7] * (corner.x + 0.5),
      scene_data.data[6] + scene_data.data[8] * (corner.y + 0.5));
    vertex_color = vec3(0.35, 0.38, 0.42);
  }

  gl_Position = PixelToClip(pixel);
}
