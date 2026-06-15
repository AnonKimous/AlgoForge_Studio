#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

void main() {
  float r2 = dot(v_uv, v_uv);
  if (r2 > 1.0) {
    discard;
  }

  float falloff = 1.0 - smoothstep(0.0, 1.0, sqrt(r2));
  vec3 inner_color = vec3(0.45, 0.95, 1.0);
  vec3 outer_color = vec3(0.08, 0.38, 0.95);
  vec3 color = mix(outer_color, inner_color, falloff);
  out_color = vec4(color, 1.0);
}
