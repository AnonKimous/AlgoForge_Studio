#version 450

layout(set = 0, binding = 0) readonly buffer RenderSceneBuffer { vec4 data[]; } render_scene_buffer;
layout(set = 0, binding = 1) readonly buffer RenderTriangleBuffer { vec4 data[]; } render_triangle_buffer;
layout(set = 0, binding = 2) readonly buffer FrameTickBuffer { uint data[]; } frame_tick_buffer;
layout(set = 0, binding = 3) readonly buffer SourceTriangleCountBuffer { uint data[]; } source_triangle_count_buffer;
layout(set = 0, binding = 4) readonly buffer RenderTriangleCountBuffer { uint data[]; } render_triangle_count_buffer;

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

vec2 ProjectPoint(vec3 point, vec3 center, float scale, uint panel) {
  vec3 q = (point - center) / max(scale, 1.0e-4);
  if (panel == 0u) return vec2(q.x, q.z);
  if (panel == 1u) return vec2(q.z, q.y);
  return vec2(q.x, q.y);
}

bool InsideTriangle(vec2 p, vec2 a, vec2 b, vec2 c) {
  float ab = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
  float bc = (c.x - b.x) * (p.y - b.y) - (c.y - b.y) * (p.x - b.x);
  float ca = (a.x - c.x) * (p.y - c.y) - (a.y - c.y) * (p.x - c.x);
  return (ab >= 0.0 && bc >= 0.0 && ca >= 0.0) ||
         (ab <= 0.0 && bc <= 0.0 && ca <= 0.0);
}

void main() {
  vec3 center = render_scene_buffer.data[0].xyz;
  float scale = render_scene_buffer.data[0].w * 1.15;
  uint panel = min(uint(v_uv.x * 3.0), 2u);
  float panel_u = fract(v_uv.x * 3.0);
  vec2 screen = vec2(panel_u, v_uv.y) * 2.0 - 1.0;
  uint triangle_count = render_triangle_count_buffer.data[0];
  vec3 color = vec3(0.025, 0.045, 0.08);
  for (uint index = 0u; index < triangle_count; ++index) {
    uint base = index * 4u;
    vec2 a = ProjectPoint(render_triangle_buffer.data[base].xyz, center, scale, panel);
    vec2 b = ProjectPoint(render_triangle_buffer.data[base + 1u].xyz, center, scale, panel);
    vec2 c = ProjectPoint(render_triangle_buffer.data[base + 2u].xyz, center, scale, panel);
    if (InsideTriangle(screen, a, b, c)) {
      color = render_triangle_buffer.data[base + 3u].xyz;
    }
  }
  color += vec3(0.01) * sin(float(frame_tick_buffer.data[0]) * 0.02);
  color = color / (color + vec3(1.0));
  color = pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2));
  out_color = vec4(color, 1.0);
}
