#version 450

layout(set = 0, binding = 0) readonly buffer RenderSceneBuffer { vec4 data[]; } render_scene_buffer;
layout(set = 0, binding = 1) readonly buffer RenderTriangleBuffer { vec4 data[]; } render_triangle_buffer;
layout(set = 0, binding = 2) readonly buffer RenderSourceTriangleBuffer { vec4 data[]; } render_source_triangle_buffer;
layout(set = 0, binding = 3) readonly buffer FrameTickBuffer { uint data[]; } frame_tick_buffer;
layout(set = 0, binding = 4) readonly buffer RenderTriangleCountBuffer { uint data[]; } render_triangle_count_buffer;
layout(set = 0, binding = 5) readonly buffer SourceTriangleCountBuffer { uint data[]; } source_triangle_count_buffer;

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

bool IntersectTriangle(vec3 ro, vec3 rd, vec3 p0, vec3 p1, vec3 p2, out float t, out vec3 normal, out vec2 barycentric) {
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
  barycentric = vec2(u, v);
  return true;
}

void main() {
  vec3 center = render_scene_buffer.data[0].xyz;
  float scene_scale = render_scene_buffer.data[0].w;
  bool custom_camera = preview_viewport.camera_position.w > 0.5;
  uint panel = min(uint(v_uv.x * 3.0), 2u);
  float panel_u = fract(v_uv.x * 3.0);
  vec3 camera = center + normalize(vec3(0.65, -1.55, 0.75)) * scene_scale;
  vec3 look_at = center;
  vec3 world_up = vec3(0.0, 0.0, 1.0);
  if (!custom_camera) {
    if (panel == 0u) camera = center + vec3(0.0, -1.55, 0.0) * scene_scale;
    if (panel == 1u) camera = center + vec3(1.55, 0.0, 0.0) * scene_scale;
    if (panel == 2u) {
      camera = center + vec3(0.0, 0.0, 1.55) * scene_scale;
      world_up = vec3(0.0, 1.0, 0.0);
    }
  }
  if (custom_camera) {
    camera = preview_viewport.camera_position.xyz;
    look_at = preview_viewport.camera_target.xyz;
    world_up = normalize(preview_viewport.camera_up.xyz);
  }
  vec3 forward = normalize(look_at - camera);
  vec3 right = normalize(cross(world_up, forward));
  vec3 up = normalize(cross(forward, right));
  vec2 screen = vec2(panel_u, v_uv.y) * 2.0 - 1.0;
  screen.x *= preview_viewport.width / preview_viewport.height / 3.0;
  vec3 ray = normalize(forward + screen.x * 0.42 * right + screen.y * 0.42 * up);
  float best_t = 1.0e30;
  vec3 best_normal = vec3(0.0, 1.0, 0.0);
  vec3 best_color = vec3(0.26, 0.86, 0.30);
  uint triangle_count = render_triangle_count_buffer.data[0];
  for (uint index = 0u; index < triangle_count; ++index) {
    uint base = index * 4u;
    float t = 0.0;
    vec3 normal = vec3(0.0);
    vec2 barycentric = vec2(0.0);
    if (!IntersectTriangle(camera, ray, render_triangle_buffer.data[base].xyz, render_triangle_buffer.data[base + 1u].xyz, render_triangle_buffer.data[base + 2u].xyz, t, normal, barycentric) || t >= best_t) continue;
    best_t = t;
    best_normal = normal;
    best_color = render_triangle_buffer.data[base + 3u].xyz;
  }
  bool source_edge_hit = false;
  uint source_count = source_triangle_count_buffer.data[0];
  for (uint index = 0u; index < source_count; ++index) {
    uint base = index * 4u;
    float source_t = 0.0;
    vec3 source_normal = vec3(0.0);
    vec2 source_barycentric = vec2(0.0);
    if (!IntersectTriangle(camera, ray, render_source_triangle_buffer.data[base].xyz, render_source_triangle_buffer.data[base + 1u].xyz, render_source_triangle_buffer.data[base + 2u].xyz, source_t, source_normal, source_barycentric)) continue;
    float edge_distance = min(source_barycentric.x, min(source_barycentric.y, 1.0 - source_barycentric.x - source_barycentric.y));
    if (edge_distance < 0.025) source_edge_hit = true;
  }
  vec3 color = vec3(0.025, 0.045, 0.08) + vec3(0.12, 0.16, 0.24) * (ray.y * 0.5 + 0.5);
  if (best_t < 1.0e29) {
    vec3 point = camera + ray * best_t;
    vec3 view_normal = dot(best_normal, camera - point) < 0.0 ? -best_normal : best_normal;
    vec3 light = normalize(vec3(-0.55, 0.82, -0.35));
    color = best_color * (0.18 + 0.82 * max(dot(view_normal, light), 0.0));
    color += vec3(0.01) * sin(float(frame_tick_buffer.data[0]) * 0.02);
  }
  if (source_edge_hit) color = vec3(0.02, 0.85, 1.0);
  color = color / (color + vec3(1.0));
  color = pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2));
  out_color = vec4(color, 1.0);
}
