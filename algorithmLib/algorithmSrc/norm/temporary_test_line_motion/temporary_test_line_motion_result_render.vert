#version 450

layout(set = 0, binding = 0) readonly buffer ProbeBuffer {
  float data[];
} probe_buffer;

void main() {
  const vec2 vertices[3] = vec2[3](
    vec2(-0.5, -0.5),
    vec2(0.5, -0.5),
    vec2(0.0, 0.5)
  );
  gl_Position = vec4(vertices[gl_VertexIndex] + vec2(probe_buffer.data[0] * 0.0), 0.0, 1.0);
}
