#define ALGORITHM_LIBRARY_PLUGIN_BUILD 1

#include "../algorithm_plugin_api.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr float kDefaultBoundaryLeft = 0.0f;
constexpr float kDefaultBoundaryRight = 1.0f;
constexpr float kDefaultBoundaryBottom = 0.0f;
constexpr float kDefaultBoundaryTop = 1.0f;
constexpr float kDefaultBallRadius = 0.035f;
constexpr float kDefaultBallSpeed = 0.18f;
constexpr float kPositionEpsilon = 1.0e-5f;
constexpr float kCollisionEpsilon = 1.0e-6f;
constexpr float kRenderPreviewDt = 1.0f / 60.0f;
constexpr size_t kMaxCollisionIterations = 12u;
constexpr size_t kBvhMetadataOffset = 4u;

struct BallState {
  float x{0.0f};
  float y{0.0f};
  float vx{0.0f};
  float vy{0.0f};
};

struct BvhNode {
  float min_x{0.0f};
  float max_x{0.0f};
  float min_y{0.0f};
  float max_y{0.0f};
  int left{-1};
  int right{-1};
  int ball_index{-1};
};

struct BoundaryRect {
  float left{kDefaultBoundaryLeft};
  float right{kDefaultBoundaryRight};
  float bottom{kDefaultBoundaryBottom};
  float top{kDefaultBoundaryTop};
};

template <typename T>
T _Clamp(T value, T min_value, T max_value) {
  return std::min(std::max(value, min_value), max_value);
}

bool _ReadScalar(
  const algorithm::AlgorithmContainerSet* container_set,
  const char* container_name,
  float fallback,
  float* out_value) {
  if (!out_value) {
    return false;
  }
  *out_value = fallback;
  if (!container_set || !container_name) {
    return true;
  }

  const algorithm::AlgorithmContainer* container = algorithm::FindAlgorithmContainer(*container_set, container_name);
  if (!container || container->bytes.size() < sizeof(float)) {
    return true;
  }

  float value = fallback;
  std::memcpy(&value, container->bytes.data(), sizeof(float));
  *out_value = value;
  return true;
}

bool _WriteScalar(
  algorithm::AlgorithmContainer* container,
  float value) {
  if (!container || container->bytes.size() < sizeof(float)) {
    return false;
  }
  std::memcpy(container->bytes.data(), &value, sizeof(float));
  return true;
}

bool _WriteScalarAtIndex(
  algorithm::AlgorithmContainer* container,
  size_t index,
  float value) {
  if (!container) {
    return false;
  }
  const size_t offset = index * static_cast<size_t>(container->element_stride);
  if (container->bytes.size() < offset + sizeof(float)) {
    return false;
  }
  std::memcpy(container->bytes.data() + offset, &value, sizeof(float));
  return true;
}

bool _ReadScalarAtIndex(
  const algorithm::AlgorithmContainer* container,
  size_t index,
  float fallback,
  float* out_value) {
  if (!out_value) {
    return false;
  }
  *out_value = fallback;
  if (!container) {
    return true;
  }
  const size_t offset = index * static_cast<size_t>(container->element_stride);
  if (container->bytes.size() < offset + sizeof(float)) {
    return true;
  }
  float value = fallback;
  std::memcpy(&value, container->bytes.data() + offset, sizeof(float));
  *out_value = value;
  return true;
}

size_t _GetArrayElementCount(const algorithm::AlgorithmContainer* container) {
  if (!container || container->storage_kind != algorithm::AlgorithmContainerStorageKind::Array) {
    return 0u;
  }
  if (container->element_stride < sizeof(float) || container->element_stride == 0u) {
    return 0u;
  }
  return container->bytes.size() / container->element_stride;
}

BoundaryRect _LoadBoundaryRect(const algorithm::AlgorithmContainerSet* container_set) {
  BoundaryRect rect{};
  _ReadScalar(container_set, "boundary_left", kDefaultBoundaryLeft, &rect.left);
  _ReadScalar(container_set, "boundary_right", kDefaultBoundaryRight, &rect.right);
  _ReadScalar(container_set, "boundary_bottom", kDefaultBoundaryBottom, &rect.bottom);
  _ReadScalar(container_set, "boundary_top", kDefaultBoundaryTop, &rect.top);
  if (rect.right <= rect.left) {
    rect.left = kDefaultBoundaryLeft;
    rect.right = kDefaultBoundaryRight;
  }
  if (rect.top <= rect.bottom) {
    rect.bottom = kDefaultBoundaryBottom;
    rect.top = kDefaultBoundaryTop;
  }
  return rect;
}

float _LoadBallRadius(const algorithm::AlgorithmContainerSet* container_set) {
  float radius = kDefaultBallRadius;
  _ReadScalar(container_set, "ball_radius", kDefaultBallRadius, &radius);
  if (!(radius > 0.0f)) {
    radius = kDefaultBallRadius;
  }
  return radius;
}

float _LoadCollisionCount(const algorithm::AlgorithmContainerSet* container_set) {
  float collision_count = 0.0f;
  _ReadScalar(container_set, "collision_count", 0.0f, &collision_count);
  if (collision_count < 0.0f) {
    collision_count = 0.0f;
  }
  return collision_count;
}

void _StoreCollisionCount(
  algorithm::AlgorithmContainerSet* container_set,
  float collision_count) {
  if (!container_set) {
    return;
  }
  algorithm::AlgorithmContainer* container = algorithm::FindAlgorithmContainer(container_set, "collision_count");
  if (container) {
    _WriteScalar(container, collision_count);
  }
}

bool _ReadBallStates(
  const algorithm::AlgorithmContainerSet* container_set,
  std::vector<BallState>* out_balls) {
  if (!container_set || !out_balls) {
    return false;
  }

  const algorithm::AlgorithmContainer* pos_x = algorithm::FindAlgorithmContainer(*container_set, "ball_pos_x");
  const algorithm::AlgorithmContainer* pos_y = algorithm::FindAlgorithmContainer(*container_set, "ball_pos_y");
  const algorithm::AlgorithmContainer* vel_x = algorithm::FindAlgorithmContainer(*container_set, "ball_vel_x");
  const algorithm::AlgorithmContainer* vel_y = algorithm::FindAlgorithmContainer(*container_set, "ball_vel_y");
  const size_t count_x = _GetArrayElementCount(pos_x);
  const size_t count_y = _GetArrayElementCount(pos_y);
  const size_t count_vx = _GetArrayElementCount(vel_x);
  const size_t count_vy = _GetArrayElementCount(vel_y);
  const size_t ball_count = std::min({count_x, count_y, count_vx, count_vy});
  if (ball_count == 0u) {
    return false;
  }

  out_balls->clear();
  out_balls->resize(ball_count);
  for (size_t i = 0; i < ball_count; ++i) {
    BallState state{};
    _ReadScalarAtIndex(pos_x, i, 0.0f, &state.x);
    _ReadScalarAtIndex(pos_y, i, 0.0f, &state.y);
    _ReadScalarAtIndex(vel_x, i, 0.0f, &state.vx);
    _ReadScalarAtIndex(vel_y, i, 0.0f, &state.vy);
    (*out_balls)[i] = state;
  }
  return true;
}

bool _WriteBallStates(
  algorithm::AlgorithmContainerSet* container_set,
  const std::vector<BallState>& balls) {
  if (!container_set) {
    return false;
  }

  algorithm::AlgorithmContainer* pos_x = algorithm::FindAlgorithmContainer(container_set, "ball_pos_x");
  algorithm::AlgorithmContainer* pos_y = algorithm::FindAlgorithmContainer(container_set, "ball_pos_y");
  algorithm::AlgorithmContainer* vel_x = algorithm::FindAlgorithmContainer(container_set, "ball_vel_x");
  algorithm::AlgorithmContainer* vel_y = algorithm::FindAlgorithmContainer(container_set, "ball_vel_y");
  if (!pos_x || !pos_y || !vel_x || !vel_y) {
    return false;
  }
  if (balls.size() > _GetArrayElementCount(pos_x) ||
      balls.size() > _GetArrayElementCount(pos_y) ||
      balls.size() > _GetArrayElementCount(vel_x) ||
      balls.size() > _GetArrayElementCount(vel_y)) {
    return false;
  }

  for (size_t i = 0; i < balls.size(); ++i) {
    if (!_WriteScalarAtIndex(pos_x, i, balls[i].x) ||
        !_WriteScalarAtIndex(pos_y, i, balls[i].y) ||
        !_WriteScalarAtIndex(vel_x, i, balls[i].vx) ||
        !_WriteScalarAtIndex(vel_y, i, balls[i].vy)) {
      return false;
    }
  }
  return true;
}

bool _IsBallStateEmpty(const BallState& state) {
  return std::abs(state.x) <= kPositionEpsilon &&
    std::abs(state.y) <= kPositionEpsilon &&
    std::abs(state.vx) <= kPositionEpsilon &&
    std::abs(state.vy) <= kPositionEpsilon;
}

void _SeedInitialBalls(
  std::vector<BallState>* balls,
  const BoundaryRect& rect,
  float radius) {
  if (!balls || balls->empty()) {
    return;
  }

  const size_t count = balls->size();
  const size_t columns = std::max<size_t>(1u, static_cast<size_t>(std::ceil(std::sqrt(static_cast<float>(count)))));
  const size_t rows = (count + columns - 1u) / columns;
  const float span_x = std::max(0.05f, rect.right - rect.left - radius * 4.0f);
  const float span_y = std::max(0.05f, rect.top - rect.bottom - radius * 4.0f);
  const float origin_x = rect.left + radius * 2.0f;
  const float origin_y = rect.bottom + radius * 2.0f;

  for (size_t i = 0; i < count; ++i) {
    const size_t column = i % columns;
    const size_t row = i / columns;
    const float u = columns > 1u ? static_cast<float>(column) / static_cast<float>(columns - 1u) : 0.5f;
    const float v = rows > 1u ? static_cast<float>(row) / static_cast<float>(rows - 1u) : 0.5f;
    BallState& state = (*balls)[i];
    state.x = origin_x + span_x * u;
    state.y = origin_y + span_y * v;
    const float phase = static_cast<float>(i) * 0.6180339887f;
    state.vx = std::cos(phase) * kDefaultBallSpeed;
    state.vy = std::sin(phase * 1.3f) * kDefaultBallSpeed;
  }
}

void _AdvanceBalls(std::vector<BallState>* balls, float dt) {
  if (!balls) {
    return;
  }
  for (BallState& ball : *balls) {
    ball.x += ball.vx * dt;
    ball.y += ball.vy * dt;
  }
}

bool _NormalizeVector(float x, float y, float* out_x, float* out_y) {
  if (!out_x || !out_y) {
    return false;
  }
  const float length_sq = x * x + y * y;
  if (length_sq <= kCollisionEpsilon) {
    *out_x = 1.0f;
    *out_y = 0.0f;
    return true;
  }
  const float inv_length = 1.0f / std::sqrt(length_sq);
  *out_x = x * inv_length;
  *out_y = y * inv_length;
  return true;
}

bool _SolveWallCollision(
  const BallState& ball,
  const BoundaryRect& rect,
  float radius,
  float remaining_time,
  float* out_time,
  bool* out_hit_x,
  bool* out_hit_y) {
  if (!out_time || !out_hit_x || !out_hit_y) {
    return false;
  }

  *out_time = std::numeric_limits<float>::infinity();
  *out_hit_x = false;
  *out_hit_y = false;

  if (ball.vx > kCollisionEpsilon) {
    const float t = (rect.right - radius - ball.x) / ball.vx;
    if (t >= -kCollisionEpsilon && t <= remaining_time) {
      *out_time = std::max(0.0f, t);
      *out_hit_x = true;
    }
  } else if (ball.vx < -kCollisionEpsilon) {
    const float t = (rect.left + radius - ball.x) / ball.vx;
    if (t >= -kCollisionEpsilon && t <= remaining_time) {
      *out_time = std::max(0.0f, t);
      *out_hit_x = true;
    }
  }

  if (ball.vy > kCollisionEpsilon) {
    const float t = (rect.top - radius - ball.y) / ball.vy;
    if (t >= -kCollisionEpsilon && t <= remaining_time) {
      if (t < *out_time - kCollisionEpsilon) {
        *out_time = std::max(0.0f, t);
        *out_hit_x = false;
      }
      if (std::abs(t - *out_time) <= 1.0e-5f) {
        *out_hit_y = true;
      }
    }
  } else if (ball.vy < -kCollisionEpsilon) {
    const float t = (rect.bottom + radius - ball.y) / ball.vy;
    if (t >= -kCollisionEpsilon && t <= remaining_time) {
      if (t < *out_time - kCollisionEpsilon) {
        *out_time = std::max(0.0f, t);
        *out_hit_x = false;
      }
      if (std::abs(t - *out_time) <= 1.0e-5f) {
        *out_hit_y = true;
      }
    }
  }

  return std::isfinite(*out_time);
}

bool _SolveBallBallCollision(
  const BallState& a,
  const BallState& b,
  float radius,
  float remaining_time,
  float* out_time,
  float* out_normal_x,
  float* out_normal_y) {
  if (!out_time || !out_normal_x || !out_normal_y) {
    return false;
  }

  const float sum_radius = radius + radius;
  const float dx = b.x - a.x;
  const float dy = b.y - a.y;
  const float dvx = b.vx - a.vx;
  const float dvy = b.vy - a.vy;

  const float c = dx * dx + dy * dy - sum_radius * sum_radius;
  if (c <= 0.0f) {
    *out_time = 0.0f;
    return _NormalizeVector(dx, dy, out_normal_x, out_normal_y);
  }

  const float a_coeff = dvx * dvx + dvy * dvy;
  if (a_coeff <= kCollisionEpsilon) {
    return false;
  }
  const float b_coeff = 2.0f * (dx * dvx + dy * dvy);
  const float discriminant = b_coeff * b_coeff - 4.0f * a_coeff * c;
  if (discriminant < 0.0f) {
    return false;
  }

  const float sqrt_discriminant = std::sqrt(std::max(0.0f, discriminant));
  const float t = (-b_coeff - sqrt_discriminant) / (2.0f * a_coeff);
  if (t < -kCollisionEpsilon || t > remaining_time) {
    return false;
  }

  const float hit_time = std::max(0.0f, t);
  const float hit_dx = dx + dvx * hit_time;
  const float hit_dy = dy + dvy * hit_time;
  *out_time = hit_time;
  return _NormalizeVector(hit_dx, hit_dy, out_normal_x, out_normal_y);
}

void _ResolveWallCollision(
  BallState* ball,
  const BoundaryRect& rect,
  float radius,
  bool hit_x,
  bool hit_y) {
  if (!ball) {
    return;
  }

  if (hit_x) {
    if (ball->vx > 0.0f) {
      ball->x = rect.right - radius;
    } else if (ball->vx < 0.0f) {
      ball->x = rect.left + radius;
    }
    ball->vx = -ball->vx;
  }

  if (hit_y) {
    if (ball->vy > 0.0f) {
      ball->y = rect.top - radius;
    } else if (ball->vy < 0.0f) {
      ball->y = rect.bottom + radius;
    }
    ball->vy = -ball->vy;
  }
}

void _ResolveBallBallCollision(
  BallState* a,
  BallState* b,
  float radius,
  float normal_x,
  float normal_y) {
  if (!a || !b) {
    return;
  }

  const float dx = b->x - a->x;
  const float dy = b->y - a->y;
  const float distance_sq = dx * dx + dy * dy;
  const float min_distance = radius + radius;
  const float min_distance_sq = min_distance * min_distance;
  if (distance_sq < min_distance_sq) {
    const float distance = std::sqrt(std::max(distance_sq, kCollisionEpsilon));
    const float penetration = min_distance - distance;
    const float correction_x = normal_x * penetration * 0.5f;
    const float correction_y = normal_y * penetration * 0.5f;
    a->x -= correction_x;
    a->y -= correction_y;
    b->x += correction_x;
    b->y += correction_y;
  }

  const float relative_vx = b->vx - a->vx;
  const float relative_vy = b->vy - a->vy;
  const float normal_velocity = relative_vx * normal_x + relative_vy * normal_y;
  if (normal_velocity >= 0.0f) {
    return;
  }

  const float impulse = -normal_velocity;
  a->vx -= impulse * normal_x;
  a->vy -= impulse * normal_y;
  b->vx += impulse * normal_x;
  b->vy += impulse * normal_y;
}

int _BuildBvhRecursive(
  const std::vector<BallState>& balls,
  const std::vector<int>& indices,
  std::vector<BvhNode>* nodes,
  float radius) {
  if (!nodes || indices.empty()) {
    return -1;
  }

  BvhNode node{};
  node.min_x = std::numeric_limits<float>::infinity();
  node.max_x = -std::numeric_limits<float>::infinity();
  node.min_y = std::numeric_limits<float>::infinity();
  node.max_y = -std::numeric_limits<float>::infinity();

  for (int index : indices) {
    const BallState& ball = balls[static_cast<size_t>(index)];
    node.min_x = std::min(node.min_x, ball.x - radius);
    node.max_x = std::max(node.max_x, ball.x + radius);
    node.min_y = std::min(node.min_y, ball.y - radius);
    node.max_y = std::max(node.max_y, ball.y + radius);
  }

  const int node_index = static_cast<int>(nodes->size());
  nodes->push_back(node);

  if (indices.size() == 1u) {
    (*nodes)[static_cast<size_t>(node_index)].ball_index = indices.front();
    return node_index;
  }

  float extent_x = node.max_x - node.min_x;
  float extent_y = node.max_y - node.min_y;
  const bool split_x = extent_x >= extent_y;
  std::vector<int> sorted_indices = indices;
  std::sort(sorted_indices.begin(), sorted_indices.end(), [&](int lhs, int rhs) {
    const BallState& a = balls[static_cast<size_t>(lhs)];
    const BallState& b = balls[static_cast<size_t>(rhs)];
    return split_x ? (a.x < b.x) : (a.y < b.y);
  });

  const size_t mid = sorted_indices.size() / 2u;
  std::vector<int> left_indices(sorted_indices.begin(), sorted_indices.begin() + static_cast<std::ptrdiff_t>(mid));
  std::vector<int> right_indices(sorted_indices.begin() + static_cast<std::ptrdiff_t>(mid), sorted_indices.end());
  const int left_child = _BuildBvhRecursive(balls, left_indices, nodes, radius);
  const int right_child = _BuildBvhRecursive(balls, right_indices, nodes, radius);
  (*nodes)[static_cast<size_t>(node_index)].left = left_child;
  (*nodes)[static_cast<size_t>(node_index)].right = right_child;
  return node_index;
}

bool _AabbOverlap(const BvhNode& a, const BvhNode& b) {
  return !(a.max_x < b.min_x || b.max_x < a.min_x || a.max_y < b.min_y || b.max_y < a.min_y);
}

void _CollectCandidatePairs(
  const std::vector<BvhNode>& nodes,
  int left_index,
  int right_index,
  std::vector<std::pair<int, int>>* out_pairs) {
  if (!out_pairs || left_index < 0 || right_index < 0) {
    return;
  }

  const BvhNode& left_node = nodes[static_cast<size_t>(left_index)];
  const BvhNode& right_node = nodes[static_cast<size_t>(right_index)];
  if (!_AabbOverlap(left_node, right_node)) {
    return;
  }

  if (left_node.ball_index >= 0 && right_node.ball_index >= 0) {
    if (left_node.ball_index != right_node.ball_index) {
      const int a = std::min(left_node.ball_index, right_node.ball_index);
      const int b = std::max(left_node.ball_index, right_node.ball_index);
      out_pairs->push_back({a, b});
    }
    return;
  }

  if (left_index == right_index) {
    if (left_node.left >= 0 && left_node.right >= 0) {
      _CollectCandidatePairs(nodes, left_node.left, left_node.left, out_pairs);
      _CollectCandidatePairs(nodes, left_node.left, left_node.right, out_pairs);
      _CollectCandidatePairs(nodes, left_node.right, left_node.right, out_pairs);
    }
    return;
  }

  if (left_node.ball_index >= 0) {
    _CollectCandidatePairs(nodes, left_index, right_node.left, out_pairs);
    _CollectCandidatePairs(nodes, left_index, right_node.right, out_pairs);
    return;
  }

  if (right_node.ball_index >= 0) {
    _CollectCandidatePairs(nodes, left_node.left, right_index, out_pairs);
    _CollectCandidatePairs(nodes, left_node.right, right_index, out_pairs);
    return;
  }

  _CollectCandidatePairs(nodes, left_node.left, right_node.left, out_pairs);
  _CollectCandidatePairs(nodes, left_node.left, right_node.right, out_pairs);
  _CollectCandidatePairs(nodes, left_node.right, right_node.left, out_pairs);
  _CollectCandidatePairs(nodes, left_node.right, right_node.right, out_pairs);
}

std::vector<std::pair<int, int>> _BuildCandidatePairs(
  const std::vector<BallState>& balls,
  float radius,
  std::vector<BvhNode>* out_nodes) {
  if (out_nodes) {
    out_nodes->clear();
  }
  if (balls.empty()) {
    return {};
  }

  std::vector<int> indices;
  indices.reserve(balls.size());
  for (size_t i = 0; i < balls.size(); ++i) {
    indices.push_back(static_cast<int>(i));
  }

  std::vector<BvhNode> nodes;
  nodes.reserve(balls.size() * 2u);
  const int root_index = _BuildBvhRecursive(balls, indices, &nodes, radius);
  if (root_index < 0) {
    return {};
  }

  std::vector<std::pair<int, int>> pairs;
  _CollectCandidatePairs(nodes, root_index, root_index, &pairs);
  std::sort(pairs.begin(), pairs.end());
  pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
  if (out_nodes) {
    *out_nodes = std::move(nodes);
  }
  return pairs;
}

void _WriteBvhDebugBuffers(
  algorithm::AlgorithmContainerSet* container_set,
  const BoundaryRect& rect,
  float radius,
  float collision_count,
  const std::vector<BvhNode>& nodes) {
  if (!container_set) {
    return;
  }

  algorithm::AlgorithmContainer* bvh_x = algorithm::FindAlgorithmContainer(container_set, "bvh_x");
  algorithm::AlgorithmContainer* bvh_y = algorithm::FindAlgorithmContainer(container_set, "bvh_y");
  if (!bvh_x || !bvh_y) {
    return;
  }

  const size_t count_x = _GetArrayElementCount(bvh_x);
  const size_t count_y = _GetArrayElementCount(bvh_y);
  const size_t capacity = std::min(count_x, count_y);
  if (capacity == 0u) {
    return;
  }

  const float metadata_x[4] = {
    rect.left,
    rect.right,
    radius,
    collision_count,
  };
  const float metadata_y[4] = {
    rect.bottom,
    rect.top,
    radius,
    collision_count,
  };
  for (size_t i = 0; i < 4u && i < capacity; ++i) {
    _WriteScalarAtIndex(bvh_x, i, metadata_x[i]);
    _WriteScalarAtIndex(bvh_y, i, metadata_y[i]);
  }

  const size_t node_offset = kBvhMetadataOffset;
  for (size_t i = 0; i < nodes.size() && node_offset + i < capacity; ++i) {
    const BvhNode& node = nodes[i];
    const float center_x = (node.min_x + node.max_x) * 0.5f;
    const float center_y = (node.min_y + node.max_y) * 0.5f;
    _WriteScalarAtIndex(bvh_x, node_offset + i, center_x);
    _WriteScalarAtIndex(bvh_y, node_offset + i, center_y);
  }
}

void _StepCollisionDemo(
  std::vector<BallState>* balls,
  const BoundaryRect& rect,
  float radius,
  float dt_seconds,
  float* in_out_collision_count,
  algorithm::AlgorithmContainerSet* container_set) {
  if (!balls || !in_out_collision_count) {
    return;
  }

  const float time_step = std::clamp(dt_seconds, 1.0f / 240.0f, 1.0f / 15.0f);
  float remaining = time_step;
  size_t iteration = 0u;

  while (remaining > kCollisionEpsilon && iteration < kMaxCollisionIterations) {
    std::vector<BvhNode> nodes;
    const std::vector<std::pair<int, int>> candidate_pairs = _BuildCandidatePairs(*balls, radius, &nodes);

    float best_time = remaining;
    bool have_wall = false;
    bool wall_hit_x = false;
    bool wall_hit_y = false;
    size_t wall_ball_index = 0u;

    for (size_t i = 0; i < balls->size(); ++i) {
      float hit_time = std::numeric_limits<float>::infinity();
      bool hit_x = false;
      bool hit_y = false;
      if (_SolveWallCollision((*balls)[i], rect, radius, remaining, &hit_time, &hit_x, &hit_y) &&
          hit_time < best_time - kCollisionEpsilon) {
        best_time = hit_time;
        have_wall = true;
        wall_hit_x = hit_x;
        wall_hit_y = hit_y;
        wall_ball_index = i;
      }
    }

    bool have_ball_collision = false;
    int collide_a = -1;
    int collide_b = -1;
    float collide_normal_x = 0.0f;
    float collide_normal_y = 0.0f;
    for (const std::pair<int, int>& pair : candidate_pairs) {
      float hit_time = std::numeric_limits<float>::infinity();
      float normal_x = 0.0f;
      float normal_y = 0.0f;
      if (!_SolveBallBallCollision(
            (*balls)[static_cast<size_t>(pair.first)],
            (*balls)[static_cast<size_t>(pair.second)],
            radius,
            remaining,
            &hit_time,
            &normal_x,
            &normal_y)) {
        continue;
      }
      if (hit_time < best_time - kCollisionEpsilon) {
        best_time = hit_time;
        have_wall = false;
        have_ball_collision = true;
        collide_a = pair.first;
        collide_b = pair.second;
        collide_normal_x = normal_x;
        collide_normal_y = normal_y;
      }
    }

    if (!have_wall && !have_ball_collision) {
      _AdvanceBalls(balls, remaining);
      remaining = 0.0f;
      break;
    }

    _AdvanceBalls(balls, best_time);
    remaining = std::max(0.0f, remaining - best_time);

    if (have_wall && wall_ball_index < balls->size()) {
      _ResolveWallCollision(
        &(*balls)[wall_ball_index],
        rect,
        radius,
        wall_hit_x,
        wall_hit_y);
      *in_out_collision_count += 1.0f;
    }

    if (have_ball_collision &&
        collide_a >= 0 && collide_b >= 0 &&
        static_cast<size_t>(collide_a) < balls->size() &&
        static_cast<size_t>(collide_b) < balls->size()) {
      _ResolveBallBallCollision(
        &(*balls)[static_cast<size_t>(collide_a)],
        &(*balls)[static_cast<size_t>(collide_b)],
        radius,
        collide_normal_x,
        collide_normal_y);
      *in_out_collision_count += 1.0f;
    }

    if (best_time <= kCollisionEpsilon) {
      _AdvanceBalls(balls, 1.0e-4f);
      remaining = std::max(0.0f, remaining - 1.0e-4f);
    }

    ++iteration;
  }

  if (remaining > kCollisionEpsilon) {
    _AdvanceBalls(balls, remaining);
  }

  for (BallState& ball : *balls) {
    ball.x = _Clamp(ball.x, rect.left + radius, rect.right - radius);
    ball.y = _Clamp(ball.y, rect.bottom + radius, rect.top - radius);
  }

  if (container_set) {
    _StoreCollisionCount(container_set, *in_out_collision_count);
    std::vector<BvhNode> final_nodes;
    (void)_BuildCandidatePairs(*balls, radius, &final_nodes);
    _WriteBvhDebugBuffers(container_set, rect, radius, *in_out_collision_count, final_nodes);
  }
}

class CollisionDemoCpuExecutor final : public agent::IAlgorithmCpuExecutor {
 public:
  bool ExecuteCpuAlgorithm(
    const agent::AgentTickContext& context,
    const algorithm::AlgorithmProfile& algorithm_profile,
    const AgentToAlgorithmSignal& agent_to_algorithm_signal,
    algorithm::AlgorithmContainerSet* algorithm_container_set,
    AlgorithmToAgentSignal* algorithm_to_agent_signal,
    agent::AlgorithmPackageDebugState* debug_state) override {
    (void)algorithm_profile;
    (void)agent_to_algorithm_signal;
    if (!algorithm_container_set) {
      return false;
    }

    std::vector<BallState> balls;
    if (!_ReadBallStates(algorithm_container_set, &balls)) {
      return false;
    }

    BoundaryRect rect = _LoadBoundaryRect(algorithm_container_set);
    const float radius = _LoadBallRadius(algorithm_container_set);
    float collision_count = _LoadCollisionCount(algorithm_container_set);

    bool all_zero = true;
    for (const BallState& ball : balls) {
      if (!_IsBallStateEmpty(ball)) {
        all_zero = false;
        break;
      }
    }
    if (all_zero) {
      _SeedInitialBalls(&balls, rect, radius);
    }

    _StepCollisionDemo(
      &balls,
      rect,
      radius,
      context.dt_seconds > 0.0f ? context.dt_seconds : kRenderPreviewDt,
      &collision_count,
      algorithm_container_set);

    if (!_WriteBallStates(algorithm_container_set, balls)) {
      return false;
    }
    _StoreCollisionCount(algorithm_container_set, collision_count);

    std::vector<BvhNode> nodes;
    (void)_BuildCandidatePairs(balls, radius, &nodes);
    _WriteBvhDebugBuffers(algorithm_container_set, rect, radius, collision_count, nodes);

    if (algorithm_to_agent_signal) {
      *algorithm_to_agent_signal = {};
    }
    if (debug_state) {
      debug_state->signals.push_back(algorithm_management::AdvancedAlgorithmDebugSignal{
        .name = "v6a6_pbd_ball_collision_demo.cpu",
        .payload = "collision_count=" + std::to_string(static_cast<int>(collision_count)),
      });
    }
    return true;
  }
};

void _DestroyCpuExecutor(agent::IAlgorithmCpuExecutor* executor) {
  delete executor;
}

}  // namespace

extern "C" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateBundle(
  const algorithmManager::support::AlgorithmPluginRequest* request,
  algorithmManager::support::AlgorithmPluginBundle* out_bundle) {
  if (!request || !out_bundle) {
    return false;
  }

  out_bundle->Clear();
  out_bundle->cpu_symbol = true;
  out_bundle->gpu_symbol = true;
<<<<<<< HEAD
  out_bundle->temporary_test_executor = new CollisionDemoMainThreadExecutor();
  out_bundle->destroy_temporary_test_executor = &_DestroyTemporaryTestExecutor;
  return true;
}
=======
  out_bundle->reflector = true;
  out_bundle->intervention = true;
  out_bundle->cpu_executor = new CollisionDemoCpuExecutor();
  out_bundle->destroy_cpu_executor = &_DestroyCpuExecutor;
  return true;
}

extern "C" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateRuntimeReflector(
  const algorithmManager::support::AlgorithmPluginRequest* request,
  algorithm::AlgorithmReflector* out_reflector) {
  if (!request || !out_reflector) {
    return false;
  }

  std::shared_ptr<algorithm::AlgorithmReflector> runtime_reflector{};
  algorithm::AlgorithmPackageLocation package_location{};
  if (!algorithm::TryResolveAlgorithmPackageLocationForPluginCompile(
        request->algorithm_name ? request->algorithm_name : "",
        &package_location,
        nullptr)) {
    return false;
  }
  if (!algorithmManager::support::LoadAlgorithmPackageReflectorFromLocation(
        package_location,
        &runtime_reflector,
        nullptr)) {
    return false;
  }
  *out_reflector = *runtime_reflector;
  return true;
}
>>>>>>> 0e5193b (preciser control of digital)
