#version 450

layout(set = 0, binding = 0) readonly buffer PackedGridStateBuffer {
  uint data[];
} packed_grid_state_buffer;

layout(set = 0, binding = 1) readonly buffer TickCounterBuffer {
  uint data[];
} tick_counter_buffer;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

bool IsActiveCell(uint mask_bits, int linear_index_from_top_left) {
  uint bit_mask = 1u << uint(15 - linear_index_from_top_left);
  return (mask_bits & bit_mask) != 0u;
}

void main() {
  const float grid_size = 4.0;
  vec2 clamped_uv = clamp(v_uv, vec2(0.0), vec2(0.999999));
  ivec2 cell = ivec2(floor(clamped_uv * grid_size));
  int row_from_top = 3 - cell.y;
  int linear_index = row_from_top * 4 + cell.x;

  uint packed_state = packed_grid_state_buffer.data[0];
  uint high_mask = (packed_state >> 16u) & 0xFFFFu;
  uint low_mask = packed_state & 0xFFFFu;
  bool high_active = IsActiveCell(high_mask, linear_index);
  bool low_active = IsActiveCell(low_mask, linear_index);

  vec2 grid_uv = clamped_uv * grid_size;
  vec2 cell_uv = fract(grid_uv);
  float edge_distance = min(
    min(cell_uv.x, 1.0 - cell_uv.x),
    min(cell_uv.y, 1.0 - cell_uv.y)
  );
  float grid_line = 1.0 - smoothstep(0.02, 0.05, edge_distance);

  float tick_phase = float(tick_counter_buffer.data[0] % 60u) / 59.0;
  vec3 background_color = vec3(0.08, 0.09, 0.11);
  vec3 inactive_cell_color = vec3(0.17, 0.18, 0.21);
  vec3 active_cell_color = high_active && low_active
    ? vec3(1.0, 0.72, 0.20)
    : vec3(0.96, 0.48, 0.10);
  vec3 grid_line_color = vec3(0.33 + 0.10 * tick_phase);

  vec3 base_color = (high_active || low_active) ? active_cell_color : inactive_cell_color;
  vec3 color = mix(base_color, grid_line_color, grid_line);

  float vignette = 1.0 - smoothstep(0.25, 1.15, distance(v_uv, vec2(0.5)));
  color = mix(background_color, color, vignette);
  out_color = vec4(color, 1.0);
}
