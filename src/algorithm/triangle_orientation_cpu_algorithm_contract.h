#pragma once

#include "algorithm_types.h"

#include <array>
#include <vector>

inline constexpr const char* kTriangleOrientationCpuAlgorithmName = "triangle_orientation_cpu";

struct TriangleOrientationCpuPositionValue {
  Vec3 value{};
};

struct TriangleOrientationCpuTriangleIndexValue {
  std::array<uint32_t, 3> value{};
};

struct TriangleOrientationCpuEdgeIndexValue {
  std::array<uint32_t, 2> value{};
};

struct TriangleOrientationCpuMatrixValue {
  uint32_t triangle_index{};
  uint32_t origin_vertex{};
  std::array<uint32_t, 2> column_vertices{};
  float m00{}, m01{};
  float m10{}, m11{};
  float determinant{};
  uint32_t faces_viewer{};
};

CreateDataReflectionInfo CreateTriangleOrientationCpuDataReflectionInfo(
  uint32_t vertex_count,
  uint32_t triangle_count,
  uint32_t edge_count);
