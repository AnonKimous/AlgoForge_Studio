#pragma once

#include "algorithm_types.h"

namespace algorithm {

inline constexpr const char* kPhysicsConvolutionGpuAlgorithmName = "physics_convolution_demo";

AlgorithmComplianceDescriptor CreatePhysicsConvolutionGpuAlgorithmComplianceDescriptor(uint32_t element_count);

}  // namespace algorithm

using algorithm::CreatePhysicsConvolutionGpuAlgorithmComplianceDescriptor;
using algorithm::kPhysicsConvolutionGpuAlgorithmName;
