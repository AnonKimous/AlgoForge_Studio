#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 1) flat in vec4 v_tint;
layout(location = 0) out vec4 out_color;

void main() {
  float r2 = dot(v_uv, v_uv);
  if (r2 > 1.0) {
    discard;
  }

  float falloff = 1.0 - smoothstep(0.0, 1.0, sqrt(r2));
  out_color = vec4(v_tint.rgb * falloff, v_tint.a * falloff);
}
