#pragma once

#include "algorithm_types.h"

#include <array>
#include <vector>

namespace algorithm {

inline constexpr const char* kTriangleOrientationCpuAlgorithmName = "triangle_orientation_cpu";

AlgorithmComplianceDescriptor CreateTriangleOrientationCpuAlgorithmComplianceDescriptor(
  uint32_t vertex_count,
  uint32_t triangle_count,
  uint32_t edge_count);

}  // namespace algorithm

using algorithm::CreateTriangleOrientationCpuAlgorithmComplianceDescriptor;
using algorithm::kTriangleOrientationCpuAlgorithmName;
