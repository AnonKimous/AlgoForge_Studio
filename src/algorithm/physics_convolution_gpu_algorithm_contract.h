#pragma once

#include "algorithm_types.h"

namespace algorithm {

inline constexpr const char* kPhysicsConvolutionGpuAlgorithmName = "physics_convolution_demo";

AlgorithmContainerDescriptor CreatePhysicsConvolutionGpuAlgorithmContainerDescriptor(uint32_t element_count);

std::vector<std::string> DescribePhysicsConvolutionGpuAlgorithmBoundResources();

}  // namespace algorithm

using algorithm::CreatePhysicsConvolutionGpuAlgorithmContainerDescriptor;
using algorithm::DescribePhysicsConvolutionGpuAlgorithmBoundResources;
using algorithm::kPhysicsConvolutionGpuAlgorithmName;
