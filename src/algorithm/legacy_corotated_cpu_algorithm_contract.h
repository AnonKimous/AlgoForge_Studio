#pragma once

#include "algorithm_types.h"

namespace algorithm {

inline constexpr const char* kLegacyCorotatedCpuAlgorithmName = "legacy_corotated_cpu";

AlgorithmComplianceDescriptor CreateLegacyCorotatedCpuAlgorithmComplianceDescriptor(
  uint32_t vertex_count,
  uint32_t triangle_count);

}  // namespace algorithm

using algorithm::CreateLegacyCorotatedCpuAlgorithmComplianceDescriptor;
using algorithm::kLegacyCorotatedCpuAlgorithmName;
