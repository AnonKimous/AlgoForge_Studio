#pragma once

#include "algorithm_types.h"

namespace algorithm {

inline constexpr const char* kCorotatedCpuAlgorithmName = "corotated_cpu";

AlgorithmContainerDescriptor CreateCorotatedCpuAlgorithmContainerDescriptor(
  uint32_t vertex_count,
  uint32_t triangle_count);

std::vector<std::string> DescribeCorotatedCpuAlgorithmBoundResources();

}  // namespace algorithm

using algorithm::CreateCorotatedCpuAlgorithmContainerDescriptor;
using algorithm::DescribeCorotatedCpuAlgorithmBoundResources;
using algorithm::kCorotatedCpuAlgorithmName;
