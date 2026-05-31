#pragma once

#include "common_data/vector_types.h"

#include <Eigen/Dense>

#include <cmath>

namespace common_data {

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
  if (std::fabs(moved[3]) > 1e-6f) {
    float inv_w = 1.0f / moved[3];
    return Vec3{moved[0] * inv_w, moved[1] * inv_w, moved[2] * inv_w};
  }
  return Vec3{moved[0], moved[1], moved[2]};
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

}  // namespace common_data

using common_data::DeltaMatrix;
using common_data::ExtractLinearVelocity;
using common_data::ExtractTranslation;
using common_data::MakeAngularVelocityMatrix;
using common_data::MakeIdentityVelocity;
using common_data::MakeLinearVelocityMatrix;
using common_data::MakeTranslationDelta;
using common_data::ComposeVelocity;
using common_data::ApplyDeltaToPoint;
using common_data::ApplyVelocityToPoint;
using common_data::VelocityMatrix;
using common_data::MakeIdentityDelta;
