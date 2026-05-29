#include "triangle_orientation_cpu_algorithm_contract.h"

#include "data_reflection.h"

CreateDataReflectionInfo CreateTriangleOrientationCpuDataReflectionInfo(
  uint32_t vertex_count,
  uint32_t triangle_count,
  uint32_t edge_count) {
  return CreateCpuJobDataReflectionInfo(
    {
      ReflectionMemoryRequest{"current_positions", vertex_count, static_cast<uint32_t>(sizeof(TriangleOrientationCpuPositionValue))},
      ReflectionMemoryRequest{"reference_positions", vertex_count, static_cast<uint32_t>(sizeof(TriangleOrientationCpuPositionValue))},
      ReflectionMemoryRequest{"triangle_indices", triangle_count, static_cast<uint32_t>(sizeof(TriangleOrientationCpuTriangleIndexValue))},
      ReflectionMemoryRequest{"edge_indices", edge_count, static_cast<uint32_t>(sizeof(TriangleOrientationCpuEdgeIndexValue))},
      ReflectionMemoryRequest{"triangle_matrices", triangle_count, static_cast<uint32_t>(sizeof(TriangleOrientationCpuMatrixValue))}
    },
    {},
    {},
    {
      ReflectionDataFormat{"current_positions", vertex_count, static_cast<uint32_t>(sizeof(TriangleOrientationCpuPositionValue))},
      ReflectionDataFormat{"reference_positions", vertex_count, static_cast<uint32_t>(sizeof(TriangleOrientationCpuPositionValue))},
      ReflectionDataFormat{"triangle_indices", triangle_count, static_cast<uint32_t>(sizeof(TriangleOrientationCpuTriangleIndexValue))},
      ReflectionDataFormat{"edge_indices", edge_count, static_cast<uint32_t>(sizeof(TriangleOrientationCpuEdgeIndexValue))}
    },
    {
      ReflectionDataFormat{"triangle_orientation_cpu_input", triangle_count, static_cast<uint32_t>(sizeof(TriangleOrientationCpuMatrixValue))}
    });
}
