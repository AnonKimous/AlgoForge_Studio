#version 450

layout(set = 0, binding = 1) buffer RenderDrawOut {
  uint data[];
} render_draw_out;

void main() {
  if (gl_VertexIndex == 0) {
    render_draw_out.data[0] = 4u;
    render_draw_out.data[1] = 37u;
    render_draw_out.data[2] = 0u;
    render_draw_out.data[3] = 0u;
  }
  gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
}
