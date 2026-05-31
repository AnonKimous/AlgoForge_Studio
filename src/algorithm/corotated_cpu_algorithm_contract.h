#pragma once

#include "algorithm_types.h"

namespace algorithm {

inline constexpr const char* kCorotatedCpuAlgorithmName = "corotated_cpu";

AlgorithmComplianceDescriptor CreateCorotatedCpuAlgorithmComplianceDescriptor(
  uint32_t vertex_count,
  uint32_t triangle_count);

}  // namespace algorithm

using algorithm::CreateCorotatedCpuAlgorithmComplianceDescriptor;
using algorithm::kCorotatedCpuAlgorithmName;
