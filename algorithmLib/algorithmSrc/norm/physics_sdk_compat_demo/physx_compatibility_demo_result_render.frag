#version 450

layout(location = 0) flat in float body_id;
layout(location = 0) out vec4 out_color;

void main() {
  if (body_id < 0.5) {
    out_color = vec4(0.16, 0.18, 0.22, 1.0);
  } else if (body_id < 1.5) {
    out_color = vec4(0.92, 0.34, 0.16, 1.0);
  } else {
    out_color = vec4(0.18, 0.55, 0.95, 1.0);
  }
}
