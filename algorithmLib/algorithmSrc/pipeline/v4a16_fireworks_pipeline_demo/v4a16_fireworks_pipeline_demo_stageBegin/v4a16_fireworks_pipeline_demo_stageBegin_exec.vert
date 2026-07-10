#version 450

layout(set = 0, binding = 32) readonly buffer V1In {
  float value[];
} v1_in;

layout(set = 0, binding = 33) buffer V1Out {
  float value[];
} v1_out;

layout(set = 0, binding = 34) readonly buffer V2In {
  float value[];
} v2_in;

layout(set = 0, binding = 35) buffer V2Out {
  float value[];
} v2_out;

layout(set = 0, binding = 36) readonly buffer V3In {
  float value[];
} v3_in;

layout(set = 0, binding = 37) buffer V3Out {
  float value[];
} v3_out;

layout(set = 0, binding = 0) readonly buffer ShellPosXIn {
  float value[];
} shell_pos_x_in;

layout(set = 0, binding = 1) buffer ShellPosXOut {
  float value[];
} shell_pos_x_out;

layout(set = 0, binding = 2) readonly buffer ShellPosYIn {
  float value[];
} shell_pos_y_in;

layout(set = 0, binding = 3) buffer ShellPosYOut {
  float value[];
} shell_pos_y_out;

layout(set = 0, binding = 4) readonly buffer ShellVelXIn {
  float value[];
} shell_vel_x_in;

layout(set = 0, binding = 5) buffer ShellVelXOut {
  float value[];
} shell_vel_x_out;

layout(set = 0, binding = 6) readonly buffer ShellVelYIn {
  float value[];
} shell_vel_y_in;

layout(set = 0, binding = 7) buffer ShellVelYOut {
  float value[];
} shell_vel_y_out;

layout(set = 0, binding = 8) readonly buffer ShellStateIn {
  float value[];
} shell_state_in;

layout(set = 0, binding = 9) buffer ShellStateOut {
  float value[];
} shell_state_out;

layout(set = 0, binding = 10) readonly buffer ShellAgeIn {
  float value[];
} shell_age_in;

layout(set = 0, binding = 11) buffer ShellAgeOut {
  float value[];
} shell_age_out;

layout(set = 0, binding = 12) readonly buffer ShellLifeIn {
  float value[];
} shell_life_in;

layout(set = 0, binding = 13) buffer ShellLifeOut {
  float value[];
} shell_life_out;

layout(set = 0, binding = 14) readonly buffer ShellSeedIn {
  float value[];
} shell_seed_in;

layout(set = 0, binding = 15) buffer ShellSeedOut {
  float value[];
} shell_seed_out;

layout(set = 0, binding = 16) readonly buffer SparkPosXIn {
  float value[];
} spark_pos_x_in;

layout(set = 0, binding = 17) buffer SparkPosXOut {
  float value[];
} spark_pos_x_out;

layout(set = 0, binding = 18) readonly buffer SparkPosYIn {
  float value[];
} spark_pos_y_in;

layout(set = 0, binding = 19) buffer SparkPosYOut {
  float value[];
} spark_pos_y_out;

layout(set = 0, binding = 20) readonly buffer SparkVelXIn {
  float value[];
} spark_vel_x_in;

layout(set = 0, binding = 21) buffer SparkVelXOut {
  float value[];
} spark_vel_x_out;

layout(set = 0, binding = 22) readonly buffer SparkVelYIn {
  float value[];
} spark_vel_y_in;

layout(set = 0, binding = 23) buffer SparkVelYOut {
  float value[];
} spark_vel_y_out;

layout(set = 0, binding = 24) readonly buffer SparkStateIn {
  float value[];
} spark_state_in;

layout(set = 0, binding = 25) buffer SparkStateOut {
  float value[];
} spark_state_out;

layout(set = 0, binding = 26) readonly buffer SparkAgeIn {
  float value[];
} spark_age_in;

layout(set = 0, binding = 27) buffer SparkAgeOut {
  float value[];
} spark_age_out;

layout(set = 0, binding = 28) readonly buffer SparkLifeIn {
  float value[];
} spark_life_in;

layout(set = 0, binding = 29) buffer SparkLifeOut {
  float value[];
} spark_life_out;

layout(set = 0, binding = 30) readonly buffer SparkSeedIn {
  float value[];
} spark_seed_in;

layout(set = 0, binding = 31) buffer SparkSeedOut {
  float value[];
} spark_seed_out;

layout(push_constant) uniform PreviewViewport {
  float width;
  float height;
} preview_viewport;

layout(location = 0) out vec2 v_uv;

const uint kArrayLimit = 512u;

vec2 PixelToLogical(vec2 pixel) {
  return vec2(
    (pixel.x / preview_viewport.width) * 2.0 - 1.0,
    (pixel.y / preview_viewport.height) * 2.0 - 1.0
  );
}

vec2 PixelVelocityToLogical(vec2 pixel_velocity) {
  return vec2(
    (pixel_velocity.x / preview_viewport.width) * 2.0,
    (pixel_velocity.y / preview_viewport.height) * 2.0
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
  float tick_value = v1_in.value[0];
  float launch_budget_value = v2_in.value[0];
  float live_spark_value = v3_in.value[0];
  bool first_tick = tick_value < 1.5;

  if (gl_VertexIndex == 0u && index == 0u) {
    v1_out.value[0] = tick_value;
    v2_out.value[0] = first_tick ? 12.0 : launch_budget_value;
    v3_out.value[0] = first_tick ? 0.0 : live_spark_value;
  }

  if (gl_VertexIndex == 0u && index < kArrayLimit) {
    float shell_state = shell_state_in.value[index];
    if (first_tick || shell_state <= 0.5) {
      shell_pos_x_out.value[index] = 0.0;
      shell_pos_y_out.value[index] = 0.0;
      shell_vel_x_out.value[index] = 0.0;
      shell_vel_y_out.value[index] = 0.0;
      shell_state_out.value[index] = 0.0;
      shell_age_out.value[index] = 0.0;
      shell_life_out.value[index] = 0.0;
      shell_seed_out.value[index] = 0.0;
    } else {
      vec2 shell_pos = PixelToLogical(vec2(shell_pos_x_in.value[index], shell_pos_y_in.value[index]));
      vec2 shell_vel = PixelVelocityToLogical(vec2(shell_vel_x_in.value[index], shell_vel_y_in.value[index]));
      shell_pos_x_out.value[index] = shell_pos.x;
      shell_pos_y_out.value[index] = shell_pos.y;
      shell_vel_x_out.value[index] = shell_vel.x;
      shell_vel_y_out.value[index] = shell_vel.y;
      shell_state_out.value[index] = shell_state;
      shell_age_out.value[index] = shell_age_in.value[index];
      shell_life_out.value[index] = shell_life_in.value[index];
      shell_seed_out.value[index] = shell_seed_in.value[index];
    }

    float spark_state = spark_state_in.value[index];
    if (first_tick || spark_state <= 0.5) {
      spark_pos_x_out.value[index] = 0.0;
      spark_pos_y_out.value[index] = 0.0;
      spark_vel_x_out.value[index] = 0.0;
      spark_vel_y_out.value[index] = 0.0;
      spark_state_out.value[index] = 0.0;
      spark_age_out.value[index] = 0.0;
      spark_life_out.value[index] = 0.0;
      spark_seed_out.value[index] = 0.0;
    } else {
      vec2 spark_pos = PixelToLogical(vec2(spark_pos_x_in.value[index], spark_pos_y_in.value[index]));
      vec2 spark_vel = PixelVelocityToLogical(vec2(spark_vel_x_in.value[index], spark_vel_y_in.value[index]));
      spark_pos_x_out.value[index] = spark_pos.x;
      spark_pos_y_out.value[index] = spark_pos.y;
      spark_vel_x_out.value[index] = spark_vel.x;
      spark_vel_y_out.value[index] = spark_vel.y;
      spark_state_out.value[index] = spark_state;
      spark_age_out.value[index] = spark_age_in.value[index];
      spark_life_out.value[index] = spark_life_in.value[index];
      spark_seed_out.value[index] = spark_seed_in.value[index];
    }
  }

  vec2 corner = quad_offsets[gl_VertexIndex];
  gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
  v_uv = corner;
}
