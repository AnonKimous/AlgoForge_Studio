#pragma once

#include "algorithm_types.h"

inline constexpr const char* kLegacyCorotatedCpuAlgorithmName = "legacy_corotated_cpu";
inline constexpr const char* kLegacyCorotatedCpuAlgorithmAlias = "legacy_corotated";

struct LegacyCorotatedCpuPositionValue {
  Vec3 value{};
};

struct LegacyCorotatedCpuTotalVelocityValue {
  VelocityMatrix value{};
};

struct LegacyCorotatedCpuJobRegisterValue {
  float values[4]{};
};

struct LegacyCorotatedCpuTriangleCacheValue {
  float values[8]{};
};

CreateDataReflectionInfo CreateLegacyCorotatedCpuDataReflectionInfo(
  uint32_t vertex_count,
  uint32_t triangle_count);
