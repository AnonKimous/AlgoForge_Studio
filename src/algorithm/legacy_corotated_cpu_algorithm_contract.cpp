#include "legacy_corotated_cpu_algorithm_contract.h"

#include "data_reflection.h"

CreateDataReflectionInfo CreateLegacyCorotatedCpuDataReflectionInfo(
  uint32_t vertex_count,
  uint32_t triangle_count) {
  return CreateCpuJobDataReflectionInfo(
    {
      ReflectionMemoryRequest{"positions", vertex_count, static_cast<uint32_t>(sizeof(LegacyCorotatedCpuPositionValue))},
      ReflectionMemoryRequest{"total_velocities", vertex_count, static_cast<uint32_t>(sizeof(LegacyCorotatedCpuTotalVelocityValue))}
    },
    {
      ReflectionMemoryRequest{"job_registers", vertex_count, static_cast<uint32_t>(sizeof(LegacyCorotatedCpuJobRegisterValue))}
    },
    {
      ReflectionMemoryRequest{"job_cache", triangle_count, static_cast<uint32_t>(sizeof(LegacyCorotatedCpuTriangleCacheValue))}
    },
    {
      ReflectionDataFormat{"positions", vertex_count, static_cast<uint32_t>(sizeof(LegacyCorotatedCpuPositionValue))},
      ReflectionDataFormat{"total_velocities", vertex_count, static_cast<uint32_t>(sizeof(LegacyCorotatedCpuTotalVelocityValue))}
    },
    {
      ReflectionDataFormat{"legacy_corotated_cpu_input", vertex_count, static_cast<uint32_t>(sizeof(LegacyCorotatedCpuPositionValue))}
    });
}
