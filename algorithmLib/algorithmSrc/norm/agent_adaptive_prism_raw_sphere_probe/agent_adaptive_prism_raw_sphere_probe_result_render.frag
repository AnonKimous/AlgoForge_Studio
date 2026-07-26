#version 450

layout(set = 0, binding = 0) readonly buffer RenderSceneBuffer { vec4 data[]; } render_scene_buffer;
layout(set = 0, binding = 1) readonly buffer RenderTriangleBuffer { vec4 data[]; } render_triangle_buffer;
layout(set = 0, binding = 2) readonly buffer FrameTickBuffer { uint data[]; } frame_tick_buffer;
layout(set = 0, binding = 3) readonly buffer RenderTriangleCountBuffer { uint data[]; } render_triangle_count_buffer;

layout(push_constant) uniform PreviewViewport { float width; float height; } preview_viewport;
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
  vec3 center = render_scene_buffer.data[0].xyz;
  float scene_scale = render_scene_buffer.data[0].w;
  vec3 camera = center + vec3(0.0, -1.55, 0.0) * scene_scale;
  vec3 forward = normalize(center - camera);
  vec3 world_up = vec3(0.0, 0.0, 1.0);
  vec3 right = normalize(cross(world_up, forward));
  vec3 up = normalize(cross(forward, right));
  vec2 screen = v_uv * 2.0 - 1.0;
  screen.x *= preview_viewport.width / preview_viewport.height;
  vec3 ray = normalize(forward + screen.x * 0.42 * right + screen.y * 0.42 * up);
  float best_t = 1.0e30;
  vec3 best_normal = vec3(0.0, 1.0, 0.0);
  vec3 best_color = vec3(0.02, 0.12, 0.22);
  uint triangle_count = render_triangle_count_buffer.data[0];
  for (uint index = 0u; index < triangle_count; ++index) {
    uint base = index * 4u;
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
    best_color = render_triangle_buffer.data[base + 3u].xyz;
  }
  vec3 color = vec3(0.025, 0.045, 0.08) + vec3(0.12, 0.16, 0.24) * (ray.y * 0.5 + 0.5);
  if (best_t < 1.0e29) {
    vec3 point = camera + ray * best_t;
    vec3 view_normal = dot(best_normal, camera - point) < 0.0 ? -best_normal : best_normal;
    vec3 light = normalize(vec3(-0.55, 0.82, -0.35));
    float diffuse = 0.18 + 0.82 * max(dot(view_normal, light), 0.0);
    color = best_color * diffuse;
    color += vec3(0.02) * sin(float(frame_tick_buffer.data[0]) * 0.02);
  }
  color = color / (color + vec3(1.0));
  color = pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2));
  out_color = vec4(color, 1.0);
}
