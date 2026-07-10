#version 450

vec2 FullscreenTriangle(uint vertex_index) {
  if (vertex_index == 0u) {
    return vec2(-1.0, -1.0);
  }
  if (vertex_index == 1u) {
    return vec2(3.0, -1.0);
  }
  return vec2(-1.0, 3.0);
}

void main() {
  vec2 pos = FullscreenTriangle(uint(gl_VertexIndex));
  gl_Position = vec4(pos, 0.0, 1.0);
}
