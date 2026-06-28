#version 450

layout(set = 0, binding = 0) readonly buffer TickSeedIn {
  float value[];
} v1_in;

layout(set = 0, binding = 1) buffer TickSeedOut {
  float value[];
} v1_out;

layout(set = 0, binding = 2) readonly buffer LaunchBudgetIn {
  float value[];
} v2_in;

layout(set = 0, binding = 3) buffer LaunchBudgetOut {
  float value[];
} v2_out;

layout(set = 0, binding = 4) readonly buffer LiveSparkCountIn {
  float value[];
} v3_in;

layout(set = 0, binding = 5) buffer LiveSparkCountOut {
  float value[];
} v3_out;

layout(set = 0, binding = 6) readonly buffer ShellPosXIn {
  float value[];
} shell_pos_x_in;

layout(set = 0, binding = 7) buffer ShellPosXOut {
  float value[];
} shell_pos_x_out;

layout(set = 0, binding = 8) readonly buffer ShellPosYIn {
  float value[];
} shell_pos_y_in;

layout(set = 0, binding = 9) buffer ShellPosYOut {
  float value[];
} shell_pos_y_out;

layout(set = 0, binding = 10) readonly buffer ShellVelXIn {
  float value[];
} shell_vel_x_in;

layout(set = 0, binding = 11) buffer ShellVelXOut {
  float value[];
} shell_vel_x_out;

layout(set = 0, binding = 12) readonly buffer ShellVelYIn {
  float value[];
} shell_vel_y_in;

layout(set = 0, binding = 13) buffer ShellVelYOut {
  float value[];
} shell_vel_y_out;

layout(set = 0, binding = 14) readonly buffer ShellStateIn {
  float value[];
} shell_state_in;

layout(set = 0, binding = 15) buffer ShellStateOut {
  float value[];
} shell_state_out;

layout(set = 0, binding = 16) readonly buffer ShellAgeIn {
  float value[];
} shell_age_in;

layout(set = 0, binding = 17) buffer ShellAgeOut {
  float value[];
} shell_age_out;

layout(set = 0, binding = 18) readonly buffer ShellLifeIn {
  float value[];
} shell_life_in;

layout(set = 0, binding = 19) buffer ShellLifeOut {
  float value[];
} shell_life_out;

layout(set = 0, binding = 20) readonly buffer ShellSeedIn {
  float value[];
} shell_seed_in;

layout(set = 0, binding = 21) buffer ShellSeedOut {
  float value[];
} shell_seed_out;

layout(set = 0, binding = 22) readonly buffer SparkPosXIn {
  float value[];
} spark_pos_x_in;

layout(set = 0, binding = 23) buffer SparkPosXOut {
  float value[];
} spark_pos_x_out;

layout(set = 0, binding = 24) readonly buffer SparkPosYIn {
  float value[];
} spark_pos_y_in;

layout(set = 0, binding = 25) buffer SparkPosYOut {
  float value[];
} spark_pos_y_out;

layout(set = 0, binding = 26) readonly buffer SparkVelXIn {
  float value[];
} spark_vel_x_in;

layout(set = 0, binding = 27) buffer SparkVelXOut {
  float value[];
} spark_vel_x_out;

layout(set = 0, binding = 28) readonly buffer SparkVelYIn {
  float value[];
} spark_vel_y_in;

layout(set = 0, binding = 29) buffer SparkVelYOut {
  float value[];
} spark_vel_y_out;

layout(set = 0, binding = 30) readonly buffer SparkStateIn {
  float value[];
} spark_state_in;

layout(set = 0, binding = 31) buffer SparkStateOut {
  float value[];
} spark_state_out;

layout(set = 0, binding = 32) readonly buffer SparkAgeIn {
  float value[];
} spark_age_in;

layout(set = 0, binding = 33) buffer SparkAgeOut {
  float value[];
} spark_age_out;

layout(set = 0, binding = 34) readonly buffer SparkLifeIn {
  float value[];
} spark_life_in;

layout(set = 0, binding = 35) buffer SparkLifeOut {
  float value[];
} spark_life_out;

layout(set = 0, binding = 36) readonly buffer SparkSeedIn {
  float value[];
} spark_seed_in;

layout(set = 0, binding = 37) buffer SparkSeedOut {
  float value[];
} spark_seed_out;

layout(push_constant) uniform AlgorithmViewport {
  float width;
  float height;
} algorithm_viewport;

layout(location = 0) out vec2 v_uv;

const uint kShellLimit = 32u;
const uint kSparkPerShell = 15u;
const uint kSparkStart = 32u;
const float kDt = 1.0 / 60.0;

float Hash(float value) {
  return fract(sin(value) * 43758.5453123);
}

float Hash2(float a, float b) {
  return Hash(a * 12.9898 + b * 78.233);
}

float Hash3(float a, float b, float c) {
  return Hash(a * 12.9898 + b * 78.233 + c * 37.719);
}

vec4 ToAlgorithmClip(vec2 algorithm_position) {
  vec2 ndc = vec2(
    (algorithm_position.x / algorithm_viewport.width) * 2.0 - 1.0,
    (algorithm_position.y / algorithm_viewport.height) * 2.0 - 1.0
  );
  return vec4(ndc, 0.0, 1.0);
}

void main() {
  const vec2 quad_offsets[4] = vec2[4](
    vec2(-1.0, -1.0),
    vec2(1.0, -1.0),
    vec2(-1.0, 1.0),
    vec2(1.0, 1.0)
  );

  uint index = uint(gl_InstanceIndex);
  uint spark_index = index - kSparkStart;

  if (gl_VertexIndex == 0u) {
    v1_out.value[0] = v1_in.value[0];
    v2_out.value[0] = v2_in.value[0];

    uint live_spark_count = 0u;
    for (uint i = kSparkStart; i < 512u; ++i) {
      if (spark_state_in.value[i] > 0.5 && spark_life_in.value[i] > 0.01) {
        live_spark_count += 1u;
      }
    }
    v3_out.value[0] = float(live_spark_count);
  }

  if (index < kShellLimit) {
    float state = shell_state_in.value[index];
    if (state > 2.5) {
      uint alive_count = 0u;
      uint spark_base = kSparkStart + index * kSparkPerShell;
      for (uint local = 0u; local < kSparkPerShell; ++local) {
        uint slot = spark_base + local;
        if (spark_state_in.value[slot] > 0.5) {
          alive_count += 1u;
        }
      }
      if (alive_count == 0u && state > 3.5) {
        shell_pos_x_out.value[index] = 0.0;
        shell_pos_y_out.value[index] = 0.0;
        shell_vel_x_out.value[index] = 0.0;
        shell_vel_y_out.value[index] = 0.0;
        shell_state_out.value[index] = 0.0;
        shell_age_out.value[index] = 0.0;
        shell_life_out.value[index] = 0.0;
        shell_seed_out.value[index] = 0.0;
      } else {
        shell_pos_x_out.value[index] = shell_pos_x_in.value[index];
        shell_pos_y_out.value[index] = shell_pos_y_in.value[index];
        shell_vel_x_out.value[index] = shell_vel_x_in.value[index];
        shell_vel_y_out.value[index] = shell_vel_y_in.value[index];
        shell_state_out.value[index] = 4.0;
        shell_age_out.value[index] = shell_age_in.value[index];
        shell_life_out.value[index] = shell_life_in.value[index];
        shell_seed_out.value[index] = shell_seed_in.value[index];
      }
    } else {
      shell_pos_x_out.value[index] = shell_pos_x_in.value[index];
      shell_pos_y_out.value[index] = shell_pos_y_in.value[index];
      shell_vel_x_out.value[index] = shell_vel_x_in.value[index];
      shell_vel_y_out.value[index] = shell_vel_y_in.value[index];
      shell_state_out.value[index] = shell_state_in.value[index];
      shell_age_out.value[index] = shell_age_in.value[index];
      shell_life_out.value[index] = shell_life_in.value[index];
      shell_seed_out.value[index] = shell_seed_in.value[index];
    }
  } else {
    shell_pos_x_out.value[index] = 0.0;
    shell_pos_y_out.value[index] = 0.0;
    shell_vel_x_out.value[index] = 0.0;
    shell_vel_y_out.value[index] = 0.0;
    shell_state_out.value[index] = 0.0;
    shell_age_out.value[index] = 0.0;
    shell_life_out.value[index] = 0.0;
    shell_seed_out.value[index] = 0.0;
  }

  if (index >= kSparkStart) {
    spark_pos_x_out.value[index] = spark_pos_x_in.value[index];
    spark_pos_y_out.value[index] = spark_pos_y_in.value[index];
    spark_vel_x_out.value[index] = spark_vel_x_in.value[index];
    spark_vel_y_out.value[index] = spark_vel_y_in.value[index];
    spark_state_out.value[index] = spark_state_in.value[index];
    spark_age_out.value[index] = spark_age_in.value[index];
    spark_life_out.value[index] = spark_life_in.value[index];
    spark_seed_out.value[index] = spark_seed_in.value[index];
  } else {
    spark_pos_x_out.value[index] = 0.0;
    spark_pos_y_out.value[index] = 0.0;
    spark_vel_x_out.value[index] = 0.0;
    spark_vel_y_out.value[index] = 0.0;
    spark_state_out.value[index] = 0.0;
    spark_age_out.value[index] = 0.0;
    spark_life_out.value[index] = 0.0;
    spark_seed_out.value[index] = 0.0;
  }

  vec2 corner = quad_offsets[gl_VertexIndex];
  gl_Position = ToAlgorithmClip(vec2(0.0, 0.0) + corner * vec2(2.0, 2.0));
  v_uv = corner;
}
