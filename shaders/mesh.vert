#version 450

layout(constant_id = 0) const float POINT_SIZE = 1.0;

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_color;
layout(location = 0) out vec3 v_color;

void main() {
  gl_Position = vec4(in_pos.x, -in_pos.y, in_pos.z, 1.0);
  gl_PointSize = POINT_SIZE;
  v_color = in_color;
}
