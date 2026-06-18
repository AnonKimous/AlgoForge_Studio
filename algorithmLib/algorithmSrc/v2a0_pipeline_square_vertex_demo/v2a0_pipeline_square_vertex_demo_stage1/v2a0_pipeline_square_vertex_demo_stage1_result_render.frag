#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

void main() {
  float r2 = dot(v_uv, v_uv);
  if (r2 > 1.0) {
    discard;
  }

  float falloff = 1.0 - smoothstep(0.0, 1.0, sqrt(r2));
  vec3 core_color = vec3(1.0, 0.95, 0.25);
  vec3 edge_color = vec3(1.0, 0.35, 0.05);
  vec3 color = mix(edge_color, core_color, falloff);
  out_color = vec4(color, 1.0);
}
