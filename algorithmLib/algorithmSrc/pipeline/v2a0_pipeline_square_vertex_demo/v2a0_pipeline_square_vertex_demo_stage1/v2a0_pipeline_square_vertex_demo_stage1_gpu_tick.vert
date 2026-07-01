#version 450

layout(set = 0, binding = 0) readonly buffer PositionXIn {
  float value[];
} position_x_in;

layout(set = 0, binding = 1) buffer PositionXOut {
  float value[];
} position_x_out;

layout(set = 0, binding = 2) readonly buffer PositionYIn {
  float value[];
} position_y_in;

layout(set = 0, binding = 3) buffer PositionYOut {
  float value[];
} position_y_out;

layout(set = 0, binding = 4) readonly buffer RollbackIn {
  float value[];
} rollback_in;

layout(set = 0, binding = 5) buffer RollbackOut {
  float value[];
} rollback_out;

layout(push_constant) uniform AlgorithmViewport {
  float width;
  float height;
} algorithm_viewport;

layout(location = 0) out vec2 v_uv;

float Clamp01(float value) {
  return clamp(value, 0.0, 1.0);
}

float WrapRange(float min_value, float max_value, float value) {
  float span = max_value - min_value;
  float normalized = (value - min_value) / span;
  float wrapped = normalized - floor(normalized);
  return min_value + wrapped * span;
}

float BuildRollbackPulse(float progress, float phase_bias) {
  float shifted = progress + phase_bias;
  shifted -= floor(shifted);
  float rise = Clamp01((shifted - 0.68) / 0.10);
  float fall = Clamp01((0.94 - shifted) / 0.10);
  return rise * fall;
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
  const float left = 50.0;
  const float right = 200.0;
  const float bottom = 50.0;
  const float top = 200.0;
  const float step = (right - left) * 0.25 * 0.50 / 60.0;
  const float rollback_constant = 6.0;

  float base_x = max(position_x_in.value[0], left);
  float base_y = max(position_y_in.value[0], bottom);
  float rollback_distance = max(rollback_in.value[0], rollback_constant);
  float normalized_x = Clamp01((base_x - left) / (right - left));
  float normalized_y = Clamp01((base_y - bottom) / (top - bottom));
  float progress = Clamp01((normalized_x + normalized_y) * 0.5);
  float rollback_pulse = BuildRollbackPulse(progress, rollback_distance * 0.03125);
  float rollback_delta = rollback_distance * rollback_pulse;
  float out_x = WrapRange(left, right, base_x + step - rollback_delta);
  float out_y = WrapRange(bottom, top, base_y + step - rollback_delta);

  if (gl_VertexIndex == 0u) {
    position_x_out.value[0] = out_x;
    position_y_out.value[0] = out_y;
    rollback_out.value[0] = rollback_distance;
  }

  vec2 corner = quad_offsets[gl_VertexIndex];
  gl_Position = ToAlgorithmClip(vec2(base_x, base_y) + corner * vec2(2.0, 2.0));
  v_uv = corner;
}
