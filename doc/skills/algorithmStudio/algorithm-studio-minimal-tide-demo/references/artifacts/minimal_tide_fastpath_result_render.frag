#version 450

layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) readonly buffer TideHeightIn {
  float value[];
} tide_height_in;

void main() {
  float tide = clamp(tide_height_in.value[0], -1.0, 1.0);
  float normalized_tide = 0.5 + tide * 0.25;
  float is_water = gl_FragCoord.y <= normalized_tide * 1080.0 ? 1.0 : 0.0;
  vec3 water = vec3(0.10, 0.35, 0.85);
  vec3 sky = vec3(0.0, 0.0, 0.0);
  out_color = vec4(mix(sky, water, is_water), 1.0);
}
