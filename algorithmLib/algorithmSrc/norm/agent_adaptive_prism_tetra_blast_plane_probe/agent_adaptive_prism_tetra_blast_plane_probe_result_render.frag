#version 450

layout(set = 0, binding = 0) readonly buffer RenderSceneBuffer { vec4 data[]; } render_scene_buffer;
layout(set = 0, binding = 1) readonly buffer RenderTriangleBuffer { vec4 data[]; } render_triangle_buffer;
layout(set = 0, binding = 2) readonly buffer FrameTickBuffer { uint data[]; } frame_tick_buffer;
layout(set = 0, binding = 3) readonly buffer RenderTriangleCountBuffer { uint data[]; } render_triangle_count_buffer;

layout(push_constant) uniform PreviewViewport {
  float width;
  float height;
  vec2 reserved;
  vec4 camera_position;
  vec4 camera_target;
  vec4 camera_up;
} preview_viewport;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

bool IntersectTriangle(vec3 ro, vec3 rd, vec3 p0, vec3 p1, vec3 p2, out float t, out vec3 normal) {
  vec3 e1 = p1 - p0;
  vec3 e2 = p2 - p0;
  vec3 p = cross(rd, e2);
  float det = dot(e1, p);
  if (abs(det) < 1.0e-8) return false;
  float inv_det = 1.0 / det;
  vec3 q = ro - p0;
  float u = dot(q, p) * inv_det;
  if (u < 0.0 || u > 1.0) return false;
  vec3 r = cross(q, e1);
  float v = dot(rd, r) * inv_det;
  if (v < 0.0 || u + v > 1.0) return false;
  t = dot(e2, r) * inv_det;
  if (t <= 1.0e-4) return false;
  normal = normalize(cross(e1, e2));
  return true;
}

void main() {
  uint panel = min(uint(v_uv.x * 3.0), 2u);
  vec3 tetra_center = render_scene_buffer.data[0].xyz;
  vec3 blast = render_scene_buffer.data[1].xyz;
  vec3 center = panel == 0u ? (tetra_center + blast) * 0.5 : tetra_center;
  float scene_scale = panel == 0u ? render_scene_buffer.data[1].w : render_scene_buffer.data[0].w;
  float panel_u = fract(v_uv.x * 3.0);
  vec3 camera = center + vec3(-1.80, 1.25, -2.35) * scene_scale;
  vec3 forward = normalize(center - camera);
  vec3 right = normalize(cross(vec3(0.0, 1.0, 0.0), forward));
  vec3 up = normalize(cross(forward, right));
  vec2 screen = vec2(panel_u, v_uv.y) * 2.0 - 1.0;
  screen.x *= preview_viewport.width / preview_viewport.height / 3.0;
  vec3 ray = normalize(forward + screen.x * 0.58 * right + screen.y * 0.58 * up);

  float best_t = 1.0e30;
  vec3 best_normal = vec3(0.0, 1.0, 0.0);
  vec3 best_color = vec3(1.0);
  uint triangle_count = render_triangle_count_buffer.data[0];
  for (uint index = 0u; index < triangle_count; ++index) {
    uint base = index * 4u;
    vec4 material = render_triangle_buffer.data[base + 3u];
    if (uint(material.w + 0.5) != panel + 1u) continue;
    float t = 0.0;
    vec3 normal = vec3(0.0);
    if (!IntersectTriangle(
          camera,
          ray,
          render_triangle_buffer.data[base + 0u].xyz,
          render_triangle_buffer.data[base + 1u].xyz,
          render_triangle_buffer.data[base + 2u].xyz,
          t,
          normal) || t >= best_t) continue;
    best_t = t;
    best_normal = normal;
    best_color = material.rgb;
  }

  vec3 color = vec3(0.035, 0.055, 0.09) + vec3(0.10, 0.14, 0.20) * (ray.y * 0.5 + 0.5);
  if (best_t < 1.0e29) {
    vec3 point = camera + ray * best_t;
    vec3 normal = dot(best_normal, camera - point) < 0.0 ? -best_normal : best_normal;
    vec3 light = normalize(vec3(-0.45, 0.82, -0.32));
    float diffuse = 0.22 + 0.78 * max(dot(normal, light), 0.0);
    color = best_color * diffuse;
  }
  float separator = min(abs(fract(v_uv.x * 3.0) - 0.002), abs(fract(v_uv.x * 3.0) - 0.998));
  if (separator < 0.003) color = vec3(0.42, 0.48, 0.58);
  color = color / (color + vec3(1.0));
  color = pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2));
  out_color = vec4(color, 1.0);
}
