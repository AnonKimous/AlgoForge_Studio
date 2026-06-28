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
const uint kWindowFrameCount = 30u;
const float kWindowLaunchBudget = 12.0;

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

vec2 PixelToLogical(vec2 pixel_position) {
  return vec2(
    pixel_position.x / algorithm_viewport.width,
    pixel_position.y / algorithm_viewport.height
  );
}

vec2 PixelVelocityToLogical(vec2 pixel_velocity) {
  return vec2(
    pixel_velocity.x / algorithm_viewport.width,
    pixel_velocity.y / algorithm_viewport.height
  );
}

void main() {
  const vec2 quad_offsets[4] = vec2[4](
    vec2(-1.0, -1.0),
    vec2(1.0, -1.0),
    vec2(-1.0, 1.0),
    vec2(1.0, 1.0)
  );

  uint index = uint(gl_InstanceIndex);
  float tick_seed = v1_in.value[0];
  bool window_reset = tick_seed < 1.0 || mod(floor(tick_seed - 1.0), float(kWindowFrameCount)) < 0.5;
  float launch_budget = window_reset ? kWindowLaunchBudget : v2_in.value[0];
  uint launch_budget_count = uint(floor(launch_budget));
  uint launch_quota = min(
    launch_budget_count,
    1u + uint(floor(Hash3(tick_seed, 23.0, 1.0) * 2.0)));
  uint inactive_count = 0u;
  for (uint other = 0u; other < kShellLimit; ++other) {
    if (shell_state_in.value[other] < 0.5) {
      inactive_count += 1u;
    }
  }
  uint spawned_count = min(launch_quota, inactive_count);

  if (gl_VertexIndex == 0u) {
    v1_out.value[0] = tick_seed + 1.0;
    v2_out.value[0] = launch_budget - float(spawned_count);
    v3_out.value[0] = v3_in.value[0];
  }

  if (index < kShellLimit) {
    float state = shell_state_in.value[index];
    float slot_score = Hash3(float(index), tick_seed, shell_seed_in.value[index]);
    uint rank = 0u;
    for (uint other = 0u; other < kShellLimit; ++other) {
      if (shell_state_in.value[other] < 0.5) {
        float other_score = Hash3(float(other), tick_seed, shell_seed_in.value[other]);
        if (other_score < slot_score || (other_score == slot_score && other < index)) {
          rank += 1u;
        }
      }
    }
    if (state < 0.5 && rank < launch_quota) {
      vec2 shell_pos = vec2(
        mix(0.06, 0.94, Hash3(float(index), tick_seed, 1.0)) * algorithm_viewport.width,
        (0.02 + Hash3(float(index), tick_seed, 2.0) * 0.06) * algorithm_viewport.height
      );
      vec2 shell_vel = vec2(
        (Hash3(float(index), tick_seed, 3.0) - 0.5) * 0.036 * algorithm_viewport.width,
        (0.050 + Hash3(float(index), tick_seed, 4.0) * 0.030) * algorithm_viewport.height
      );
      vec2 shell_logical_pos = PixelToLogical(shell_pos);
      vec2 shell_logical_vel = PixelVelocityToLogical(shell_vel);
      shell_pos_x_out.value[index] = shell_logical_pos.x;
      shell_pos_y_out.value[index] = shell_logical_pos.y;
      shell_vel_x_out.value[index] = shell_logical_vel.x;
      shell_vel_y_out.value[index] = shell_logical_vel.y;
      shell_state_out.value[index] = 1.0;
      shell_age_out.value[index] = 0.0;
      shell_life_out.value[index] = 100.0 + Hash3(float(index), tick_seed, 5.0) * 60.0;
      shell_seed_out.value[index] = Hash3(float(index), tick_seed, 6.0);
    } else {
      vec2 shell_pos = PixelToLogical(vec2(shell_pos_x_in.value[index], shell_pos_y_in.value[index]));
      vec2 shell_vel = PixelVelocityToLogical(vec2(shell_vel_x_in.value[index], shell_vel_y_in.value[index]));
      shell_pos_x_out.value[index] = shell_pos.x;
      shell_pos_y_out.value[index] = shell_pos.y;
      shell_vel_x_out.value[index] = shell_vel.x;
      shell_vel_y_out.value[index] = shell_vel.y;
      shell_state_out.value[index] = shell_state_in.value[index];
      shell_age_out.value[index] = shell_age_in.value[index];
      shell_life_out.value[index] = shell_life_in.value[index];
      shell_seed_out.value[index] = shell_seed_in.value[index];
    }

    if (gl_VertexIndex == 0u) {
      float remaining_budget = launch_budget - float(spawned_count);
      v2_out.value[0] = remaining_budget > 0.0 ? remaining_budget : 0.0;
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

  if (spark_state_in.value[index] > 0.5) {
    vec2 spark_pos = PixelToLogical(vec2(spark_pos_x_in.value[index], spark_pos_y_in.value[index]));
    vec2 spark_vel = PixelVelocityToLogical(vec2(spark_vel_x_in.value[index], spark_vel_y_in.value[index]));
    spark_pos_x_out.value[index] = spark_pos.x;
    spark_pos_y_out.value[index] = spark_pos.y;
    spark_vel_x_out.value[index] = spark_vel.x;
    spark_vel_y_out.value[index] = spark_vel.y;
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
