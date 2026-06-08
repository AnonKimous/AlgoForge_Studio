#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

void main() {
  // Turn the square quad into a white disc with a soft spherical feel.
  float r2 = dot(v_uv, v_uv);
  if (r2 > 1.0) {
    discard;
  }

  float z = sqrt(max(0.0, 1.0 - r2));
  vec3 normal = normalize(vec3(v_uv, z));
  vec3 light_dir = normalize(vec3(-0.25, 0.35, 0.9));

  float diffuse = 0.35 + 0.65 * max(dot(normal, light_dir), 0.0);
  float alpha = smoothstep(1.0, 0.72, sqrt(r2));

  // White sphere, slightly shaded so it still reads as a round object.
  out_color = vec4(vec3(diffuse), alpha);
}
