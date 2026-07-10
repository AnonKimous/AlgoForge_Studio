#version 450

layout(set = 0, binding = 0) readonly buffer SceneInfoBuffer {
  vec4 data[];
} scene_info_buffer;

layout(set = 0, binding = 1) readonly buffer MaterialBuffer {
  vec4 data[];
} material_buffer;

layout(set = 0, binding = 2) readonly buffer TriangleBuffer {
  vec4 data[];
} triangle_buffer;

layout(set = 0, binding = 3) readonly buffer BvhBuffer {
  vec4 data[];
} bvh_buffer;

layout(set = 0, binding = 4) readonly buffer FrameTickBuffer {
  uint data[];
} frame_tick_buffer;

layout(set = 0, binding = 5) readonly buffer TriangleCountBuffer {
  uint data[];
} triangle_count_buffer;

layout(set = 0, binding = 6) readonly buffer BvhCountBuffer {
  uint data[];
} bvh_count_buffer;

layout(set = 0, binding = 7) readonly buffer TextureSeedBuffer {
  uint data[];
} texture_seed_buffer;

layout(push_constant) uniform PreviewViewport {
  float width;
  float height;
} preview_viewport;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

float Saturate(float value) {
  return clamp(value, 0.0, 1.0);
}

vec3 Saturate(vec3 value) {
  return clamp(value, vec3(0.0), vec3(1.0));
}

vec3 SceneCenter() {
  return scene_info_buffer.data[0].xyz;
}

float SceneRadius() {
  return scene_info_buffer.data[0].w;
}

float MaterialRoughness() {
  return material_buffer.data[0].w;
}

vec3 MaterialKa() {
  return material_buffer.data[0].xyz;
}

vec3 MaterialKd() {
  return material_buffer.data[1].xyz;
}

float MaterialMetallic() {
  return material_buffer.data[1].w;
}

vec3 MaterialKs() {
  return material_buffer.data[2].xyz;
}

float MaterialOpacity() {
  return material_buffer.data[2].w;
}

vec3 SkyColor(vec3 dir) {
  float height = Saturate(dir.y * 0.5 + 0.5);
  float haze = smoothstep(-0.35, 0.55, dir.y);
  vec3 horizon = vec3(0.60, 0.68, 0.76);
  vec3 zenith = vec3(0.16, 0.20, 0.30);
  vec3 sky = mix(horizon, zenith, pow(height, 1.35));
  sky = mix(vec3(0.72, 0.78, 0.84), sky, haze);
  vec3 sun_dir = normalize(vec3(-0.22, 0.90, 0.36));
  float sun = pow(max(dot(dir, sun_dir), 0.0), 220.0);
  float bloom = pow(max(dot(dir, sun_dir), 0.0), 18.0);
  sky += vec3(1.0, 0.94, 0.82) * sun * 4.0;
  sky += vec3(0.34, 0.40, 0.48) * bloom * 0.20;
  return sky;
}

vec3 FloorColor(vec3 point, float distance_to_camera) {
  float checker_scale = 0.85;
  vec2 grid = point.xz * checker_scale;
  float checker = mod(floor(grid.x) + floor(grid.y), 2.0);
  vec3 dark_tile = vec3(0.10, 0.10, 0.11);
  vec3 light_tile = vec3(0.92, 0.91, 0.88);
  vec3 tile = mix(dark_tile, light_tile, checker);
  float sky_fade = smoothstep(2.5, 20.0, distance_to_camera);
  return mix(tile, vec3(0.60, 0.68, 0.76), sky_fade);
}

vec3 EnvironmentColor(vec3 origin, vec3 dir) {
  if (dir.y >= -0.0001) {
    return SkyColor(dir);
  }

  float t = -origin.y / dir.y;
  vec3 hit = origin + dir * t;
  vec3 floor_color = FloorColor(hit, t);
  float floor_weight = Saturate(exp(-t * 0.055));
  float sky_weight = 1.0 - floor_weight;
  return mix(SkyColor(dir), floor_color, floor_weight * 0.95 + sky_weight * 0.05);
}

float DistributionGGX(float NdotH, float roughness) {
  float a = roughness * roughness;
  float a2 = a * a;
  float denom = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
  return a2 / max(3.14159265359 * denom * denom, 0.0001);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
  float r = roughness + 1.0;
  float k = (r * r) / 8.0;
  return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(float NdotV, float NdotL, float roughness) {
  return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

vec3 FresnelSchlick(float cos_theta, vec3 f0) {
  return f0 + (1.0 - f0) * pow(1.0 - cos_theta, 5.0);
}

vec3 ProceduralGoldTile(vec2 uv) {
  uint seed = texture_seed_buffer.data[0];
  float tile_scale = 11.0 + float(seed & 7u) * 0.5;
  vec2 tiled = uv * tile_scale + vec2(float((seed >> 3u) & 15u), float((seed >> 7u) & 15u)) * 0.03125;
  vec2 cell = fract(tiled);
  float bevel = smoothstep(0.0, 0.18, min(min(cell.x, cell.y), min(1.0 - cell.x, 1.0 - cell.y)));
  float groove = smoothstep(0.0, 0.08, min(abs(cell.x - 0.5), abs(cell.y - 0.5)));
  float grain = 0.06 * sin((uv.x + uv.y) * 120.0 + float(seed & 255u) * 0.07);
  vec3 gold_a = MaterialKd() * vec3(1.08, 0.96, 0.72);
  vec3 gold_b = MaterialKd() * vec3(0.78, 0.62, 0.18);
  vec3 gold = mix(gold_b, gold_a, bevel);
  gold += grain * vec3(0.12, 0.09, 0.03);
  gold *= mix(vec3(0.90), vec3(1.08), groove);
  return Saturate(gold);
}

vec3 SampleTriangleAlbedo(vec2 uv) {
  return Saturate(MaterialKd() * 0.88 + MaterialKa() * 0.12);
}

bool IntersectAabb(vec3 ro, vec3 rd, vec3 inv_rd, vec3 mn, vec3 mx, out float tmin, out float tmax) {
  vec3 t0 = (mn - ro) * inv_rd;
  vec3 t1 = (mx - ro) * inv_rd;
  vec3 lo = min(t0, t1);
  vec3 hi = max(t0, t1);
  tmin = max(max(lo.x, lo.y), lo.z);
  tmax = min(min(hi.x, hi.y), hi.z);
  return tmax >= max(tmin, 0.0);
}

bool IntersectTriangle(
  vec3 ro,
  vec3 rd,
  vec3 p0,
  vec3 p1,
  vec3 p2,
  out float t,
  out float u,
  out float v,
  out float w) {
  vec3 e1 = p1 - p0;
  vec3 e2 = p2 - p0;
  vec3 pvec = cross(rd, e2);
  float det = dot(e1, pvec);
  if (abs(det) < 1.0e-8) {
    return false;
  }
  float inv_det = 1.0 / det;
  vec3 tvec = ro - p0;
  u = dot(tvec, pvec) * inv_det;
  if (u < 0.0 || u > 1.0) {
    return false;
  }
  vec3 qvec = cross(tvec, e1);
  v = dot(rd, qvec) * inv_det;
  if (v < 0.0 || u + v > 1.0) {
    return false;
  }
  t = dot(e2, qvec) * inv_det;
  if (t <= 1.0e-4) {
    return false;
  }
  w = 1.0 - u - v;
  return true;
}

struct RayHit {
  float t;
  int tri_index;
  vec3 bary;
  vec3 normal;
  vec2 uv;
};

RayHit TraceTeapot(vec3 ro, vec3 rd) {
  RayHit hit;
  hit.t = 1.0e30;
  hit.tri_index = -1;
  hit.bary = vec3(0.0);
  hit.normal = vec3(0.0, 1.0, 0.0);
  hit.uv = vec2(0.0);

  vec3 inv_rd = 1.0 / rd;
  int stack[64];
  int stack_size = 0;
  stack[stack_size++] = 0;
  uint node_count = bvh_count_buffer.data[0];
  uint tri_count = triangle_count_buffer.data[0];

  while (stack_size > 0) {
    int node_index = stack[--stack_size];
    if (node_index < 0 || uint(node_index) >= node_count) {
      continue;
    }
    vec4 a = bvh_buffer.data[node_index * 2 + 0];
    vec4 b = bvh_buffer.data[node_index * 2 + 1];
    float tmin = 0.0;
    float tmax = 0.0;
    if (!IntersectAabb(ro, rd, inv_rd, a.xyz, b.xyz, tmin, tmax) || tmin > hit.t) {
      continue;
    }
    if (b.w < 0.0) {
      int tri_index = int(a.w + 0.5);
      if (tri_index < 0 || uint(tri_index) >= tri_count) {
        continue;
      }
      int base = tri_index * 6;
      vec3 p0 = triangle_buffer.data[base + 0].xyz;
      vec3 p1 = triangle_buffer.data[base + 1].xyz;
      vec3 p2 = triangle_buffer.data[base + 2].xyz;
      float t = 0.0;
      float u = 0.0;
      float v = 0.0;
      float w = 0.0;
      if (!IntersectTriangle(ro, rd, p0, p1, p2, t, u, v, w) || t >= hit.t) {
        continue;
      }
      vec3 n0 = triangle_buffer.data[base + 3].xyz;
      vec3 n1 = triangle_buffer.data[base + 4].xyz;
      vec3 n2 = triangle_buffer.data[base + 5].xyz;
      vec3 normal = normalize(n0 * w + n1 * u + n2 * v);
      vec2 uv0 = vec2(triangle_buffer.data[base + 0].w, triangle_buffer.data[base + 3].w);
      vec2 uv1 = vec2(triangle_buffer.data[base + 1].w, triangle_buffer.data[base + 4].w);
      vec2 uv2 = vec2(triangle_buffer.data[base + 2].w, triangle_buffer.data[base + 5].w);
      hit.t = t;
      hit.tri_index = tri_index;
      hit.bary = vec3(w, u, v);
      hit.normal = normal;
      hit.uv = uv0 * w + uv1 * u + uv2 * v;
      continue;
    }

    int left = int(a.w + 0.5);
    int right = int(b.w + 0.5);
    if (stack_size < 62) {
      stack[stack_size++] = right;
      stack[stack_size++] = left;
    }
  }

  return hit;
}

float SoftShadow(vec3 ro, vec3 rd, float min_t, float max_t) {
  float shadow = 1.0;
  float t = min_t;
  for (int i = 0; i < 20; ++i) {
    vec3 p = ro + rd * t;
    RayHit hit = TraceTeapot(p, rd);
    if (hit.tri_index >= 0 && hit.t < 0.02) {
      return 0.0;
    }
    t += max(0.05, min(0.25, t * 0.18));
    if (t >= max_t) {
      break;
    }
    shadow = min(shadow, 1.0 - 0.15 * float(i));
  }
  return Saturate(shadow);
}

vec3 ShadePbr(
  vec3 albedo,
  float metallic,
  float roughness,
  vec3 normal,
  vec3 view_dir,
  vec3 world_pos,
  vec3 environment_bias) {
  vec3 light_dir = normalize(vec3(-0.58, 0.86, -0.28));
  vec3 half_dir = normalize(view_dir + light_dir);
  float NdotL = Saturate(dot(normal, light_dir));
  float NdotV = Saturate(dot(normal, view_dir));
  float NdotH = Saturate(dot(normal, half_dir));
  float VdotH = Saturate(dot(view_dir, half_dir));

  vec3 f0 = mix(vec3(0.04), albedo, metallic);
  vec3 fresnel = FresnelSchlick(VdotH, f0);
  float D = DistributionGGX(NdotH, roughness);
  float G = GeometrySmith(NdotV, NdotL, roughness);
  vec3 specular = (D * G * fresnel) /
    max(4.0 * max(NdotV, 0.001) * max(NdotL, 0.001), 0.001);

  vec3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * albedo / 3.14159265359;
  float shadow = SoftShadow(world_pos + normal * 0.015, light_dir, 0.02, 18.0);
  vec3 direct = (diffuse + specular) * vec3(5.1, 4.8, 4.5) * NdotL * shadow;
  vec3 reflection = EnvironmentColor(world_pos + normal * 0.016, reflect(-view_dir, normal));
  vec3 ambient = reflection * fresnel * (0.55 + 0.45 * (1.0 - roughness));
  ambient += environment_bias * (1.0 - metallic) * 0.20;
  return direct + ambient;
}

vec3 ShadeFloor(vec3 world_pos, vec3 view_dir) {
  vec3 floor_normal = vec3(0.0, 1.0, 0.0);
  vec3 reflection = EnvironmentColor(world_pos + floor_normal * 0.02, reflect(-view_dir, floor_normal));
  vec3 checker = FloorColor(world_pos, length(world_pos));
  vec3 f0 = mix(vec3(0.04), checker, 0.04);
  vec3 normal = floor_normal;
  vec3 half_dir = normalize(view_dir + normalize(vec3(-0.58, 0.86, -0.28)));
  float NdotL = Saturate(dot(normal, normalize(vec3(-0.58, 0.86, -0.28))));
  float NdotV = Saturate(dot(normal, view_dir));
  float NdotH = Saturate(dot(normal, half_dir));
  float VdotH = Saturate(dot(view_dir, half_dir));
  vec3 fresnel = FresnelSchlick(VdotH, f0);
  float roughness = 0.92;
  float D = DistributionGGX(NdotH, roughness);
  float G = GeometrySmith(NdotV, NdotL, roughness);
  vec3 specular = (D * G * fresnel) /
    max(4.0 * max(NdotV, 0.001) * max(NdotL, 0.001), 0.001);
  vec3 diffuse = checker / 3.14159265359;
  vec3 direct = (diffuse + specular) * vec3(1.8) * NdotL * 0.95;
  vec3 ambient = reflection * 0.42;
  return mix(direct + ambient, checker, 0.28);
}

void main() {
  float frame_index = float(frame_tick_buffer.data[0] % 120u);
  float orbit = frame_index / 120.0 * 6.28318530718;
  vec3 center = SceneCenter();
  float radius = SceneRadius();
  vec3 camera_origin = center + vec3(
    cos(orbit) * (radius * 1.75 + 2.6),
    radius * 0.55 + 1.75 + 0.24 * sin(orbit * 0.5),
    sin(orbit) * (radius * 1.75 + 2.6));
  vec3 forward = normalize(center - camera_origin);
  vec3 world_up = vec3(0.0, 1.0, 0.0);
  vec3 right = normalize(cross(world_up, forward));
  vec3 up = normalize(cross(forward, right));

  vec2 screen = v_uv * 2.0 - 1.0;
  float aspect = preview_viewport.width / preview_viewport.height;
  screen.x *= aspect;

  float focal = 1.10;
  vec3 ray_dir = normalize(forward + screen.x * focal * right + screen.y * focal * up);

  vec3 color = SkyColor(ray_dir);
  float floor_t = 1.0e30;
  bool floor_hit = false;
  if (ray_dir.y < -0.0001) {
    floor_t = -camera_origin.y / ray_dir.y;
    floor_hit = floor_t > 0.0;
  }

  RayHit teapot_hit = TraceTeapot(camera_origin, ray_dir);
  bool hit_teapot = teapot_hit.tri_index >= 0 && teapot_hit.t < 1.0e29;

  if (floor_hit && (!hit_teapot || floor_t < teapot_hit.t)) {
    vec3 hit_pos = camera_origin + ray_dir * floor_t;
    color = ShadeFloor(hit_pos, normalize(camera_origin - hit_pos));
  } else if (hit_teapot) {
    vec3 hit_pos = camera_origin + ray_dir * teapot_hit.t;
    vec3 view_dir = normalize(camera_origin - hit_pos);
    vec3 normal = normalize(teapot_hit.normal);
    if (dot(normal, view_dir) < 0.0) {
      normal = -normal;
    }
    vec3 albedo = SampleTriangleAlbedo(teapot_hit.uv);
    vec3 floor_bias = FloorColor(vec3(hit_pos.xz, 0.0), length(hit_pos - camera_origin)) * 0.12;
    color = ShadePbr(
      albedo,
      MaterialMetallic(),
      MaterialRoughness(),
      normal,
      view_dir,
      hit_pos,
      floor_bias);
    color += MaterialKs() * 0.03;
    color = mix(color, albedo, 1.0 - MaterialOpacity());
  }

  float fog = Saturate((length(camera_origin - (camera_origin + ray_dir * max(min(teapot_hit.t, floor_t), 0.0))) - radius * 0.8) / (radius * 8.0 + 18.0));
  color = mix(color, SkyColor(ray_dir), fog * 0.22);
  color = color / (color + vec3(1.0));
  color = pow(Saturate(color), vec3(1.0 / 2.2));
  out_color = vec4(color, 1.0);
}
