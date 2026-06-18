#version 450

layout(set = 0, binding = 0) readonly buffer BallPosXIn {
  float data[];
} ball_pos_x_in;

layout(set = 0, binding = 1) buffer BallPosXOut {
  float data[];
} ball_pos_x_out;

layout(set = 0, binding = 2) readonly buffer BallPosYIn {
  float data[];
} ball_pos_y_in;

layout(set = 0, binding = 3) buffer BallPosYOut {
  float data[];
} ball_pos_y_out;

layout(set = 0, binding = 4) readonly buffer BallVelXIn {
  float data[];
} ball_vel_x_in;

layout(set = 0, binding = 5) buffer BallVelXOut {
  float data[];
} ball_vel_x_out;

layout(set = 0, binding = 6) readonly buffer BallVelYIn {
  float data[];
} ball_vel_y_in;

layout(set = 0, binding = 7) buffer BallVelYOut {
  float data[];
} ball_vel_y_out;

layout(set = 0, binding = 8) readonly buffer BvhXIn {
  float data[];
} bvh_x_in;

layout(set = 0, binding = 9) buffer BvhXOut {
  float data[];
} bvh_x_out;

layout(set = 0, binding = 10) readonly buffer BvhYIn {
  float data[];
} bvh_y_in;

layout(set = 0, binding = 11) buffer BvhYOut {
  float data[];
} bvh_y_out;

layout(push_constant) uniform AlgorithmViewport {
  float width;
  float height;
} algorithm_viewport;

layout(location = 0) out vec2 v_uv;

const uint kBallCount = 37u;
const float kDt = 1.0 / 60.0;
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

bool ReadBoundary(out float left, out float right, out float bottom, out float top) {
  left = 0.0;
  right = 1.0;
  bottom = 0.0;
  top = 1.0;
  if (bvh_x_in.data.length() > 1) {
    left = bvh_x_in.data[0];
    right = bvh_x_in.data[1];
  }
  if (bvh_y_in.data.length() > 1) {
    bottom = bvh_y_in.data[0];
    top = bvh_y_in.data[1];
  }
  if (!(right > left) || !(top > bottom)) {
    left = 0.0;
    right = 1.0;
    bottom = 0.0;
    top = 1.0;
  }
  return true;
}

float ReadRadius(float span_x, float span_y) {
  float radius = DefaultRadius(span_x, span_y);
  if (bvh_x_in.data.length() > 2) {
    radius = bvh_x_in.data[2];
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
  const float screen_scale = min(
    algorithm_viewport.width / max(right - left, 1.0e-6),
    algorithm_viewport.height / max(top - bottom, 1.0e-6));

  vec2 pos = vec2(ball_pos_x_in.data[index], ball_pos_y_in.data[index]);
  vec2 vel = vec2(ball_vel_x_in.data[index], ball_vel_y_in.data[index]);
  if (!IsSeeded(pos, vel)) {
    pos = DefaultPosition(index, left, right, bottom, top, radius);
    vel = DefaultVelocity(index, right - left, top - bottom);
  }

  pos += vel * kDt;

  if (pos.x - radius < left) {
    pos.x = left + radius;
    vel.x = abs(vel.x);
  } else if (pos.x + radius > right) {
    pos.x = right - radius;
    vel.x = -abs(vel.x);
  }
  if (pos.y - radius < bottom) {
    pos.y = bottom + radius;
    vel.y = abs(vel.y);
  } else if (pos.y + radius > top) {
    pos.y = top - radius;
    vel.y = -abs(vel.y);
  }

  for (uint other_index = 0u; other_index < kBallCount; ++other_index) {
    if (other_index == index) {
      continue;
    }

    vec2 other_pos = vec2(ball_pos_x_in.data[other_index], ball_pos_y_in.data[other_index]);
    vec2 other_vel = vec2(ball_vel_x_in.data[other_index], ball_vel_y_in.data[other_index]);
    if (!IsSeeded(other_pos, other_vel)) {
      other_pos = DefaultPosition(other_index, left, right, bottom, top, radius);
      other_vel = DefaultVelocity(other_index, right - left, top - bottom);
    }

    vec2 delta = pos - other_pos;
    float dist2 = dot(delta, delta);
    float min_dist = radius * 2.0;
    float min_dist2 = min_dist * min_dist;
    if (dist2 < min_dist2 && dist2 > 1.0e-6) {
      float dist = sqrt(dist2);
      vec2 n = delta / dist;
      float penetration = min_dist - dist;
      pos += n * (penetration * 0.5);
      float rel_normal = dot(vel - other_vel, n);
      if (rel_normal < 0.0) {
        vel -= rel_normal * n;
      }
    }
  }

  if (gl_VertexIndex == 0u) {
    ball_pos_x_out.data[index] = pos.x;
    ball_pos_y_out.data[index] = pos.y;
    ball_vel_x_out.data[index] = vel.x;
    ball_vel_y_out.data[index] = vel.y;

    if (index == 0u) {
      bvh_x_out.data[0] = left;
      bvh_x_out.data[1] = right;
      bvh_x_out.data[2] = radius;
      bvh_x_out.data[3] = uint(bvh_x_in.data.length()) > 3u ? bvh_x_in.data[3] : 0.0;
      bvh_y_out.data[0] = bottom;
      bvh_y_out.data[1] = top;
      bvh_y_out.data[2] = radius;
      bvh_y_out.data[3] = uint(bvh_y_in.data.length()) > 3u ? bvh_y_in.data[3] : 0.0;
    }

    if (uint(bvh_x_out.data.length()) > 4u + index) {
      bvh_x_out.data[4u + index] = pos.x;
    }
    if (uint(bvh_y_out.data.length()) > 4u + index) {
      bvh_y_out.data[4u + index] = pos.y;
    }
  }

  const vec2 quad_offsets[4] = vec2[4](
    vec2(-1.0, -1.0),
    vec2(1.0, -1.0),
    vec2(-1.0, 1.0),
    vec2(1.0, 1.0)
  );
  vec2 pixel_pos = vec2(pos.x - left, pos.y - bottom) * screen_scale;
  vec2 corner = quad_offsets[gl_VertexIndex];
  vec2 radius_px = vec2(radius * screen_scale, radius * screen_scale);
  gl_Position = vec4(
    (pixel_pos + corner * radius_px) / vec2(algorithm_viewport.width, algorithm_viewport.height) * 2.0 - 1.0,
    0.0,
    1.0
  );
  v_uv = corner;
}
