#pragma once

#include "algorithm_types.h"

inline constexpr const char* kPhysicsConvolutionGpuAlgorithmName = "physics_convolution_demo";
inline constexpr const char* kPhysicsConvolutionGpuAlgorithmAlias = "convolution_demo";

struct PhysicsConvolutionGpuInputValue {
  float value{};
};

struct PhysicsConvolutionGpuRegisterValue {
  float value{};
};

struct PhysicsConvolutionGpuCacheValue {
  float value{};
};

CreateDataReflectionInfo CreatePhysicsConvolutionGpuDataReflectionInfo(uint32_t element_count);
