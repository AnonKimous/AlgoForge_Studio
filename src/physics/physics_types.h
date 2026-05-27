#pragma once

#include <Eigen/Dense>

#include "../math_types.h"

#include <cmath>
#include <cstdint>
#include <vector>

using DeltaMatrix = Eigen::Matrix4f;

inline DeltaMatrix MakeIdentityDelta() {
  return DeltaMatrix::Identity();
}

inline DeltaMatrix MakeTranslationDelta(Vec3 translation) {
  DeltaMatrix delta = DeltaMatrix::Identity();
  delta(0, 3) = translation.x;
  delta(1, 3) = translation.y;
  delta(2, 3) = translation.z;
  return delta;
}

inline Vec3 ApplyDeltaToPoint(const DeltaMatrix& delta, Vec3 position) {
  Eigen::Vector4f point{position.x, position.y, position.z, 1.0f};
  Eigen::Vector4f moved = delta * point;
  if (std::fabs(moved.w) > 1e-6f) {
    float inv_w = 1.0f / moved.w;
    return Vec3{moved.x * inv_w, moved.y * inv_w, moved.z * inv_w};
  }
  return Vec3{moved.x, moved.y, moved.z};
}

inline Vec3 ExtractTranslation(const DeltaMatrix& delta) {
  return Vec3{delta(0, 3), delta(1, 3), delta(2, 3)};
}

enum class PhysRunState {
  Run,
  Pause,
  Stop,
};

struct PhysDirective {
  int vertex{-1};
  Vec3 start{};
  Vec3 requested_target{};
  Vec3 allowed_target{};
  DeltaMatrix delta{};
  bool hidden{false};
  bool valid{};
};

struct PhysicsRestTriangle {
  uint32_t origin{};
  uint32_t first{};
  uint32_t second{};
  float inv00{}, inv01{};
  float inv10{}, inv11{};
  float material_gpa{1000000.0f};
};

struct PhysicsStepInput {
  std::vector<Vec3> positions;
  std::vector<DeltaMatrix> vertex_deltas;
  std::vector<PhysicsRestTriangle> rest_triangles;
  std::vector<PhysDirective> directives;
};

struct PhysicsStepOutput {
  std::vector<Vec3> positions;
  std::vector<DeltaMatrix> vertex_deltas;
  std::vector<PhysDirective> directives;
};
