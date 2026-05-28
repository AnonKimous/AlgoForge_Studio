#pragma once

#include <Eigen/Dense>

#include "../math_types.h"

#include <cmath>
#include <cstdint>
#include <vector>

using VelocityMatrix = Eigen::Matrix4f;
using DeltaMatrix = VelocityMatrix;

inline VelocityMatrix MakeIdentityVelocity() {
  return VelocityMatrix::Identity();
}

inline VelocityMatrix MakeLinearVelocityMatrix(Vec3 translation) {
  VelocityMatrix velocity = VelocityMatrix::Identity();
  velocity(0, 3) = translation.x;
  velocity(1, 3) = translation.y;
  velocity(2, 3) = translation.z;
  return velocity;
}

inline VelocityMatrix MakeAngularVelocityMatrix(float radians) {
  VelocityMatrix velocity = VelocityMatrix::Identity();
  float c = std::cos(radians);
  float s = std::sin(radians);
  velocity(0, 0) = c;
  velocity(0, 1) = -s;
  velocity(1, 0) = s;
  velocity(1, 1) = c;
  return velocity;
}

inline VelocityMatrix ComposeVelocity(const VelocityMatrix& angular_velocity, const VelocityMatrix& linear_velocity) {
  return linear_velocity * angular_velocity;
}

inline DeltaMatrix MakeIdentityDelta() {
  return MakeIdentityVelocity();
}

inline DeltaMatrix MakeTranslationDelta(Vec3 translation) {
  return MakeLinearVelocityMatrix(translation);
}

inline Vec3 ApplyVelocityToPoint(const VelocityMatrix& velocity, Vec3 position) {
  Eigen::Vector4f point{position.x, position.y, position.z, 1.0f};
  Eigen::Vector4f moved = velocity * point;
  if (std::fabs(moved.w) > 1e-6f) {
    float inv_w = 1.0f / moved.w;
    return Vec3{moved.x * inv_w, moved.y * inv_w, moved.z * inv_w};
  }
  return Vec3{moved.x, moved.y, moved.z};
}

inline Vec3 ApplyDeltaToPoint(const DeltaMatrix& delta, Vec3 position) {
  return ApplyVelocityToPoint(delta, position);
}

inline Vec3 ExtractLinearVelocity(const VelocityMatrix& velocity) {
  return Vec3{velocity(0, 3), velocity(1, 3), velocity(2, 3)};
}

inline Vec3 ExtractTranslation(const DeltaMatrix& delta) {
  return ExtractLinearVelocity(delta);
}

enum class PhysRunState {
  Run,
  Pause,
};

struct VelocityGuidance {
  int vertex{-1};
  std::vector<int> vertices;
  Vec3 start{};
  Vec3 requested_target{};
  Vec3 allowed_target{};
  VelocityMatrix total_velocity{};
  bool hidden{false};
  bool valid{};
};

using PhysDirective = VelocityGuidance;

struct VelocityGuideVelocity {
  std::vector<int> vertices;
  VelocityMatrix velocity{};
  uint32_t start_frame_offset{0};
  uint32_t duration_frames{1};
  bool hidden{false};
  bool valid{true};
};

struct VelocityGuideForce {
  std::vector<int> vertices;
  Vec3 force{};
  uint32_t start_frame_offset{0};
  uint32_t duration_frames{1};
  bool hidden{false};
  bool valid{true};
};

struct PhysicsRestTriangle {
  uint32_t origin{};
  uint32_t first{};
  uint32_t second{};
  float inv00{}, inv01{};
  float inv10{}, inv11{};
  float rest_area{};
  float material_gpa{1000000.0f};
};

struct PhysicsStepInput {
  std::vector<Vec3> positions;
  std::vector<VelocityMatrix> total_velocities;
  std::vector<VelocityMatrix> linear_velocities;
  std::vector<VelocityMatrix> angular_velocities;
  std::vector<PhysicsRestTriangle> rest_triangles;
  std::vector<VelocityGuidance> guidances;
  std::vector<VelocityGuideVelocity> guide_velocities;
  std::vector<VelocityGuideForce> guide_forces;
};

struct PhysicsStepOutput {
  std::vector<Vec3> positions;
  std::vector<VelocityMatrix> total_velocities;
  std::vector<VelocityMatrix> linear_velocities;
  std::vector<VelocityMatrix> angular_velocities;
  std::vector<VelocityGuidance> guidances;
  std::vector<VelocityGuideVelocity> guide_velocities;
  std::vector<VelocityGuideForce> guide_forces;
};
