#version 450

layout(set = 0, binding = 0) readonly buffer V1In {
  float value[];
} v1_in;

layout(set = 0, binding = 1) buffer V1Out {
  float value[];
} v1_out;

layout(set = 0, binding = 2) readonly buffer V2In {
  float value[];
} v2_in;

layout(set = 0, binding = 3) buffer V2Out {
  float value[];
} v2_out;

layout(set = 0, binding = 4) readonly buffer V3In {
  float value[];
} v3_in;

layout(set = 0, binding = 5) buffer V3Out {
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

const uint kArrayLimit = 512u;

vec2 LogicalToPixel(vec2 logical_position) {
  return (logical_position * 0.5 + vec2(0.5)) * vec2(algorithm_viewport.width, algorithm_viewport.height);
}

vec2 LogicalVelocityToPixel(vec2 logical_velocity) {
  return logical_velocity * vec2(algorithm_viewport.width, algorithm_viewport.height) * 0.5;
}

void main() {
  const vec2 quad_offsets[4] = vec2[4](
    vec2(-1.0, -1.0),
    vec2(1.0, -1.0),
    vec2(-1.0, 1.0),
    vec2(1.0, 1.0)
  );

  uint index = uint(gl_InstanceIndex);

  if (gl_VertexIndex == 0u) {
    v1_out.value[0] = v1_in.value[0];
    v2_out.value[0] = v2_in.value[0];
    v3_out.value[0] = v3_in.value[0];
  }

  if (index < kArrayLimit) {
    float shell_state = shell_state_in.value[index];
    if (shell_state > 0.5) {
      vec2 shell_pos = LogicalToPixel(vec2(shell_pos_x_in.value[index], shell_pos_y_in.value[index]));
      vec2 shell_vel = LogicalVelocityToPixel(vec2(shell_vel_x_in.value[index], shell_vel_y_in.value[index]));
      shell_pos_x_out.value[index] = shell_pos.x;
      shell_pos_y_out.value[index] = shell_pos.y;
      shell_vel_x_out.value[index] = shell_vel.x;
      shell_vel_y_out.value[index] = shell_vel.y;
      shell_state_out.value[index] = shell_state;
      shell_age_out.value[index] = shell_age_in.value[index];
      shell_life_out.value[index] = shell_life_in.value[index];
      shell_seed_out.value[index] = shell_seed_in.value[index];
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

    float spark_state = spark_state_in.value[index];
    if (spark_state > 0.5) {
      vec2 spark_pos = LogicalToPixel(vec2(spark_pos_x_in.value[index], spark_pos_y_in.value[index]));
      vec2 spark_vel = LogicalVelocityToPixel(vec2(spark_vel_x_in.value[index], spark_vel_y_in.value[index]));
      spark_pos_x_out.value[index] = spark_pos.x;
      spark_pos_y_out.value[index] = spark_pos.y;
      spark_vel_x_out.value[index] = spark_vel.x;
      spark_vel_y_out.value[index] = spark_vel.y;
      spark_state_out.value[index] = spark_state;
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
  }

  vec2 corner = quad_offsets[gl_VertexIndex];
  gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
  v_uv = corner;
}
