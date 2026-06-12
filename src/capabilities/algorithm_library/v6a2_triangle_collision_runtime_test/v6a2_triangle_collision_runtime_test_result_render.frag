#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 1) flat in uint v_kind;
layout(location = 2) flat in vec3 v_color;

layout(location = 0) out vec4 out_color;

void main() {
  if (v_kind == 0u) {
    float r2 = dot(v_uv, v_uv);
    if (r2 > 1.0) {
      discard;
    }

    float falloff = 1.0 - smoothstep(0.0, 1.0, sqrt(r2));
    vec3 color = mix(v_color * 0.7, v_color, falloff);
    out_color = vec4(color, 1.0);
    return;
  }

  float line_alpha = 1.0 - smoothstep(0.72, 1.0, abs(v_uv.y));
  if (line_alpha <= 0.0) {
    discard;
  }

  vec3 color = mix(v_color * 0.65, v_color, line_alpha);
  out_color = vec4(color, line_alpha);
}
