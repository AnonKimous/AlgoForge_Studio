#version 450

layout(set = 0, binding = 0) readonly buffer ShellPosX {
  float value[];
} shell_pos_x;

layout(set = 0, binding = 1) readonly buffer ShellPosY {
  float value[];
} shell_pos_y;

layout(set = 0, binding = 2) readonly buffer ShellVelX {
  float value[];
} shell_vel_x;

layout(set = 0, binding = 3) readonly buffer ShellVelY {
  float value[];
} shell_vel_y;

layout(set = 0, binding = 4) readonly buffer ShellState {
  float value[];
} shell_state;

layout(set = 0, binding = 5) readonly buffer ShellAge {
  float value[];
} shell_age;

layout(set = 0, binding = 6) readonly buffer ShellLife {
  float value[];
} shell_life;

layout(set = 0, binding = 7) readonly buffer ShellSeed {
  float value[];
} shell_seed;

layout(set = 0, binding = 8) readonly buffer SparkPosX {
  float value[];
} spark_pos_x;

layout(set = 0, binding = 9) readonly buffer SparkPosY {
  float value[];
} spark_pos_y;

layout(set = 0, binding = 10) readonly buffer SparkVelX {
  float value[];
} spark_vel_x;

layout(set = 0, binding = 11) readonly buffer SparkVelY {
  float value[];
} spark_vel_y;

layout(set = 0, binding = 12) readonly buffer SparkState {
  float value[];
} spark_state;

layout(set = 0, binding = 13) readonly buffer SparkAge {
  float value[];
} spark_age;

layout(set = 0, binding = 14) readonly buffer SparkLife {
  float value[];
} spark_life;

layout(set = 0, binding = 15) readonly buffer SparkSeed {
  float value[];
} spark_seed;

layout(set = 0, binding = 16) readonly buffer LaunchBudget {
  float value[];
} v2;

layout(push_constant) uniform PreviewViewport {
  float width;
  float height;
} preview_viewport;

layout(location = 0) out vec2 v_uv;
layout(location = 1) flat out vec4 v_tint;

const uint kArrayLimit = 512u;

float Hash(float value) {
  return fract(sin(value) * 43758.5453123);
}

vec4 ToPreviewClip(vec2 preview_position) {
  vec2 ndc = vec2(
    (preview_position.x / preview_viewport.width) * 2.0 - 1.0,
    (preview_position.y / preview_viewport.height) * 2.0 - 1.0
  );
  return vec4(ndc, 0.0, 1.0);
}

vec2 ToPreviewPixel(vec2 position) {
  if (abs(position.x) <= 2.0 && abs(position.y) <= 2.0) {
    return (position * 0.5 + vec2(0.5)) * vec2(preview_viewport.width, preview_viewport.height);
  }
  return position;
}

void main() {
  const vec2 quad_offsets[4] = vec2[4](
    vec2(-1.0, -1.0),
    vec2(1.0, -1.0),
    vec2(-1.0, 1.0),
    vec2(1.0, 1.0)
  );

  uint index = uint(gl_InstanceIndex);
  float scene_scale = 1.0;
  vec2 center = vec2(0.0);
  float radius = 1.0;

  if (index < kArrayLimit && shell_state.value[index] > 0.5 && shell_state.value[index] < 1.5) {
    center = vec2(shell_pos_x.value[index], shell_pos_y.value[index]);
    radius = (16.0 + Hash(shell_seed.value[index] + float(index)) * 8.0) * scene_scale;
    float fade = clamp(1.0 - shell_age.value[index] / max(shell_life.value[index], 0.001), 0.0, 1.0) * scene_scale;
    vec3 hot = vec3(1.0, 0.96, 0.45);
    vec3 warm = vec3(1.0, 0.48, 0.08);
    vec3 color = mix(warm, hot, Hash(shell_seed.value[index] * 7.0));
    v_tint = vec4(color, clamp(fade * fade * 1.5, 0.0, 1.0));
  } else if (index < kArrayLimit && shell_state.value[index] > 1.5) {
    float flash = clamp(1.0 - shell_age.value[index] / max(shell_life.value[index], 0.001), 0.0, 1.0);
    center = vec2(shell_pos_x.value[index], shell_pos_y.value[index]);
    radius = (26.0 + Hash(shell_seed.value[index] + float(index)) * 14.0) * (0.45 + flash * 0.75);
    vec3 core = vec3(1.0, 0.92, 0.28);
    vec3 rim = vec3(1.0, 0.42, 0.05);
    vec3 color = mix(rim, core, flash);
    v_tint = vec4(color, clamp(flash * flash * 2.0, 0.0, 1.0));
  } else if (index < kArrayLimit && spark_state.value[index] > 0.5) {
    center = vec2(spark_pos_x.value[index], spark_pos_y.value[index]);
    radius = (4.0 + Hash(spark_seed.value[index] + float(index)) * 4.0) * scene_scale;
    float fade = clamp(1.0 - spark_age.value[index] / max(spark_life.value[index], 0.001), 0.0, 1.0) * scene_scale;
    vec3 edge = vec3(1.0, 0.32, 0.05);
    vec3 core = vec3(1.0, 0.86, 0.22);
    vec3 color = mix(edge, core, Hash(spark_seed.value[index] * 11.0));
    v_tint = vec4(color, clamp(fade * fade * 1.5, 0.0, 1.0));
  } else {
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    v_uv = vec2(0.0);
    v_tint = vec4(0.0);
    return;
  }

  vec2 corner = quad_offsets[gl_VertexIndex];
  gl_Position = ToPreviewClip(ToPreviewPixel(center) + corner * radius);
  v_uv = corner;
}
