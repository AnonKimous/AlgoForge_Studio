#version 450

layout(set = 0, binding = 0) readonly buffer RenderSceneBuffer {
  vec4 data[];
} render_scene_buffer;

layout(set = 0, binding = 1) readonly buffer RenderTriangleBuffer {
  vec4 data[];
} render_triangle_buffer;

layout(set = 0, binding = 2) readonly buffer RenderBvhBuffer {
  vec4 data[];
} render_bvh_buffer;

layout(set = 0, binding = 3) readonly buffer FrameTickBuffer {
  uint data[];
} frame_tick_buffer;

layout(set = 0, binding = 4) readonly buffer RenderTriangleCountBuffer {
  uint data[];
} render_triangle_count_buffer;

layout(set = 0, binding = 5) readonly buffer RenderBvhCountBuffer {
  uint data[];
} render_bvh_count_buffer;

layout(push_constant) uniform PreviewViewport {
  float width;
  float height;
} preview_viewport;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

struct RayHit {
  float t;
  vec3 normal;
  vec3 color;
};

vec3 Background(vec3 direction) {
  float horizon = clamp(direction.y * 0.5 + 0.5, 0.0, 1.0);
  return mix(vec3(0.06, 0.08, 0.12), vec3(0.38, 0.48, 0.62), horizon);
}

bool IntersectAabb(vec3 ro, vec3 rd, vec3 mn, vec3 mx, out float t_min, out float t_max) {
  vec3 inv_rd = 1.0 / rd;
  vec3 t0 = (mn - ro) * inv_rd;
  vec3 t1 = (mx - ro) * inv_rd;
  vec3 lo = min(t0, t1);
  vec3 hi = max(t0, t1);
  t_min = max(max(lo.x, lo.y), lo.z);
  t_max = min(min(hi.x, hi.y), hi.z);
  return t_max >= max(t_min, 0.0);
}

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

RayHit TraceScene(vec3 origin, vec3 direction) {
  RayHit hit;
  hit.t = 1.0e30;
  hit.normal = vec3(0.0, 1.0, 0.0);
  hit.color = vec3(0.0);
  uint bvh_count = render_bvh_count_buffer.data[0];
  uint triangle_count = render_triangle_count_buffer.data[0];
  int stack[64];
  int stack_size = 0;
  if (bvh_count == 0u) return hit;
  stack[stack_size++] = 0;
  while (stack_size > 0) {
    int node_index = stack[--stack_size];
    if (node_index < 0 || uint(node_index) >= bvh_count) continue;
    vec4 a = render_bvh_buffer.data[node_index * 2 + 0];
    vec4 b = render_bvh_buffer.data[node_index * 2 + 1];
    float t_min = 0.0;
    float t_max = 0.0;
    if (!IntersectAabb(origin, direction, a.xyz, b.xyz, t_min, t_max) || t_min > hit.t) continue;
    if (b.w < 0.0) {
      uint triangle_index = uint(a.w + 0.5);
      if (triangle_index >= triangle_count) continue;
      uint base = triangle_index * 4u;
      vec3 p0 = render_triangle_buffer.data[base + 0u].xyz;
      vec3 p1 = render_triangle_buffer.data[base + 1u].xyz;
      vec3 p2 = render_triangle_buffer.data[base + 2u].xyz;
      float t = 0.0;
      vec3 normal = vec3(0.0);
      if (!IntersectTriangle(origin, direction, p0, p1, p2, t, normal) || t >= hit.t) continue;
      hit.t = t;
      hit.normal = normal;
      hit.color = render_triangle_buffer.data[base + 3u].xyz;
    } else {
      if (stack_size < 62) {
        stack[stack_size++] = int(b.w + 0.5);
        stack[stack_size++] = int(a.w + 0.5);
      }
    }
  }
  return hit;
}

void main() {
  vec3 center = render_scene_buffer.data[0].xyz;
  float radius = render_scene_buffer.data[0].w;
  vec3 camera = center + vec3(-1.85, 1.10, -2.65);
  vec3 forward = normalize(center - camera);
  vec3 world_up = vec3(0.0, 1.0, 0.0);
  vec3 right = normalize(cross(world_up, forward));
  vec3 up = normalize(cross(forward, right));
  vec2 screen = v_uv * 2.0 - 1.0;
  screen.x *= preview_viewport.width / preview_viewport.height;
  vec3 ray = normalize(forward + screen.x * 0.95 * right + screen.y * 0.95 * up);
  RayHit hit = TraceScene(camera, ray);
  vec3 color = Background(ray);
  if (hit.t < 1.0e29) {
    vec3 point = camera + ray * hit.t;
    vec3 normal = dot(hit.normal, camera - point) < 0.0 ? -hit.normal : hit.normal;
    vec3 light = normalize(vec3(-0.45, 0.82, -0.32));
    float diffuse = 0.22 + 0.78 * max(dot(normal, light), 0.0);
    float edge = pow(1.0 - max(dot(normal, normalize(camera - point)), 0.0), 2.0);
    color = hit.color * diffuse + vec3(0.18, 0.22, 0.30) * edge;
    color += vec3(0.06) * sin(float(frame_tick_buffer.data[0]) * 0.04);
  } else if (ray.y < -0.0001) {
    float floor_t = (-0.62 - camera.y) / ray.y;
    if (floor_t > 0.0) {
      vec3 floor_point = camera + ray * floor_t;
      float checker = mod(floor(floor_point.x * 2.0) + floor(floor_point.z * 2.0), 2.0);
      color = mix(vec3(0.07, 0.08, 0.10), vec3(0.18, 0.20, 0.23), checker);
    }
  }
  color = color / (color + vec3(1.0));
  color = pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2));
  out_color = vec4(color, 1.0);
}
