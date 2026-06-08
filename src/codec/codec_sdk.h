#pragma once

#include "common_data/vector_types.h"

#include <cmath>
#include <cstddef>
#include <vector>

namespace codec {

namespace sdk {

inline Vec3 Add(const Vec3& lhs, const Vec3& rhs) {
  return Vec3{lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

inline Vec3 Subtract(const Vec3& lhs, const Vec3& rhs) {
  return Vec3{lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

inline Vec3 Scale(const Vec3& value, float factor) {
  return Vec3{value.x * factor, value.y * factor, value.z * factor};
}

inline Vec3 Midpoint(const Vec3& lhs, const Vec3& rhs) {
  return Scale(Add(lhs, rhs), 0.5f);
}

inline float Dot(const Vec3& lhs, const Vec3& rhs) {
  return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

inline Vec3 Cross(const Vec3& lhs, const Vec3& rhs) {
  return Vec3{
    lhs.y * rhs.z - lhs.z * rhs.y,
    lhs.z * rhs.x - lhs.x * rhs.z,
    lhs.x * rhs.y - lhs.y * rhs.x,
  };
}

inline float LengthSquared(const Vec3& value) {
  return Dot(value, value);
}

inline Vec3 NormalizeOrFallback(const Vec3& value, const Vec3& fallback = Vec3{0.0f, 0.0f, 1.0f}) {
  const float length_squared = LengthSquared(value);
  if (length_squared <= 0.0f) {
    return fallback;
  }
  const float inv_length = 1.0f / std::sqrt(length_squared);
  return Scale(value, inv_length);
}

inline Vec3 WeightedCentroid(
  const std::vector<Vec3>& positions,
  const std::vector<float>& weights) {
  Vec3 weighted_sum{0.0f, 0.0f, 0.0f};
  float total_weight = 0.0f;
  for (size_t i = 0; i < positions.size(); ++i) {
    const float weight = weights[i];
    weighted_sum = Add(weighted_sum, Scale(positions[i], weight));
    total_weight += weight;
  }
  return Scale(weighted_sum, 1.0f / total_weight);
}

inline Vec3 Mean(const std::vector<Vec3>& positions) {
  Vec3 sum{0.0f, 0.0f, 0.0f};
  for (const Vec3& position : positions) {
    sum = Add(sum, position);
  }
  return Scale(sum, 1.0f / static_cast<float>(positions.size()));
}

}  // namespace sdk

}  // namespace codec
