#version 450

layout(set = 0, binding = 0) readonly buffer BallPosX {
  float data[];
} ball_pos_x;

layout(set = 0, binding = 1) readonly buffer BallPosY {
  float data[];
} ball_pos_y;

layout(set = 0, binding = 2) readonly buffer BallVelX {
  float data[];
} ball_vel_x;

layout(set = 0, binding = 3) readonly buffer BallVelY {
  float data[];
} ball_vel_y;

layout(set = 0, binding = 4) readonly buffer BvhX {
  float data[];
} bvh_x;

layout(set = 0, binding = 5) readonly buffer BvhY {
  float data[];
} bvh_y;

layout(push_constant) uniform PreviewViewport {
  float width;
  float height;
} preview_viewport;

layout(location = 0) out vec2 v_uv;

const uint kBallCount = 37u;
const float kDefaultRadiusFactor = 0.035;
const float kDefaultSpeedFactor = 0.18;

float DefaultRadius(float span_x, float span_y) {
  return max(0.01, min(span_x, span_y) * kDefaultRadiusFactor);
}

vec2 DefaultPosition(uint index, float left, float right, float bottom, float top, float radius) {
  float width = max(right - left, radius * 4.0);
  float height = max(top - bottom, radius * 4.0);
  uint columns = uint(ceil(sqrt(float(kBallCount))));
  uint rows = (kBallCount + columns - 1u) / columns;
  uint column = index % columns;
  uint row = index / columns;
  float u = columns > 1u ? float(column) / float(columns - 1u) : 0.5;
  float v = rows > 1u ? float(row) / float(rows - 1u) : 0.5;
  return vec2(
    left + radius * 2.0 + (width - radius * 4.0) * u,
    bottom + radius * 2.0 + (height - radius * 4.0) * v
  );
}

vec2 DefaultVelocity(uint index, float span_x, float span_y) {
  float speed = max(0.05, min(span_x, span_y) * kDefaultSpeedFactor);
  float phase = float(index) * 0.6180339887;
  return vec2(cos(phase), sin(phase * 1.3)) * speed;
}

bool IsSeeded(vec2 pos, vec2 vel) {
  return dot(pos, pos) > 1.0e-6 || dot(vel, vel) > 1.0e-6;
}

void ReadBoundary(out float left, out float right, out float bottom, out float top) {
  left = 0.0;
  right = 1.0;
  bottom = 0.0;
  top = 1.0;
  if (bvh_x.data.length() > 1) {
    left = bvh_x.data[0];
    right = bvh_x.data[1];
  }
  if (bvh_y.data.length() > 1) {
    bottom = bvh_y.data[0];
    top = bvh_y.data[1];
  }
  if (!(right > left) || !(top > bottom)) {
    left = 0.0;
    right = 1.0;
    bottom = 0.0;
    top = 1.0;
  }
}

float ReadRadius(float span_x, float span_y) {
  float radius = DefaultRadius(span_x, span_y);
  if (bvh_x.data.length() > 2) {
    radius = bvh_x.data[2];
  }
  if (!(radius > 0.0)) {
    radius = DefaultRadius(span_x, span_y);
  }
  return radius;
}

void main() {
  uint index = uint(gl_InstanceIndex);
  if (index >= kBallCount) {
    v_uv = vec2(0.0);
    gl_Position = vec4(0.0);
    return;
  }

  float left;
  float right;
  float bottom;
  float top;
  ReadBoundary(left, right, bottom, top);
  float radius = ReadRadius(right - left, top - bottom);

  vec2 pos = vec2(ball_pos_x.data[index], ball_pos_y.data[index]);
  vec2 vel = vec2(ball_vel_x.data[index], ball_vel_y.data[index]);
  if (!IsSeeded(pos, vel)) {
    pos = DefaultPosition(index, left, right, bottom, top, radius);
    vel = DefaultVelocity(index, right - left, top - bottom);
  }

  const vec2 quad_offsets[4] = vec2[4](
    vec2(-1.0, -1.0),
    vec2(1.0, -1.0),
    vec2(-1.0, 1.0),
    vec2(1.0, 1.0)
  );

  vec2 corner = quad_offsets[gl_VertexIndex];
  vec2 radius_px = vec2(
    radius / max(right - left, 1.0e-6) * preview_viewport.width,
    radius / max(top - bottom, 1.0e-6) * preview_viewport.height
  );
  vec2 pixel = vec2(
    (pos.x - left) / max(right - left, 1.0e-6) * preview_viewport.width,
    (pos.y - bottom) / max(top - bottom, 1.0e-6) * preview_viewport.height
  );
  gl_Position = vec4(
    (pixel + corner * radius_px) / vec2(preview_viewport.width, preview_viewport.height) * 2.0 - 1.0,
    0.0,
    1.0
  );
  v_uv = corner;
}
