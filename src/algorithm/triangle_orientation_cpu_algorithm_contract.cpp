#include "triangle_orientation_cpu_algorithm_contract.h"

#include "data_protocol/triangle_orientation_state.h"

#include <array>

namespace algorithm {

AlgorithmComplianceDescriptor CreateTriangleOrientationCpuAlgorithmComplianceDescriptor(
  uint32_t vertex_count,
  uint32_t triangle_count,
  uint32_t edge_count) {
  AlgorithmComplianceDescriptor descriptor{};
  descriptor.algorithm_name = kTriangleOrientationCpuAlgorithmName;
  descriptor.cpu_available = true;
  descriptor.gpu_available = false;
  descriptor.data_contract.arrays_to_allocate = {
    AlgorithmBufferRequirement{"current_positions", vertex_count, static_cast<uint32_t>(sizeof(Vec3))},
    AlgorithmBufferRequirement{"reference_positions", vertex_count, static_cast<uint32_t>(sizeof(Vec3))},
    AlgorithmBufferRequirement{"triangle_indices", triangle_count, static_cast<uint32_t>(sizeof(std::array<uint32_t, 3>))},
    AlgorithmBufferRequirement{"edge_indices", edge_count, static_cast<uint32_t>(sizeof(std::array<uint32_t, 2>))},
    AlgorithmBufferRequirement{"triangle_matrices", triangle_count, static_cast<uint32_t>(sizeof(TriangleMatrix2D))}
  };
  descriptor.data_contract.temporary_registers_to_allocate = {};
  descriptor.data_contract.temporary_caches_to_allocate = {};
  descriptor.data_contract.filled_data_formats = {
    AlgorithmDataFormat{"current_positions", vertex_count, static_cast<uint32_t>(sizeof(Vec3))},
    AlgorithmDataFormat{"reference_positions", vertex_count, static_cast<uint32_t>(sizeof(Vec3))},
    AlgorithmDataFormat{"triangle_indices", triangle_count, static_cast<uint32_t>(sizeof(std::array<uint32_t, 3>))},
    AlgorithmDataFormat{"edge_indices", edge_count, static_cast<uint32_t>(sizeof(std::array<uint32_t, 2>))}
  };
  descriptor.data_contract.algorithm_required_formats = {
    AlgorithmDataFormat{"triangle_orientation_cpu_input", triangle_count, static_cast<uint32_t>(sizeof(TriangleMatrix2D))}
  };
  return descriptor;
}

}  // namespace algorithm
