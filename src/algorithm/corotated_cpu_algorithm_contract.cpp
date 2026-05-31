#include "corotated_cpu_algorithm_contract.h"

namespace algorithm {

AlgorithmComplianceDescriptor CreateCorotatedCpuAlgorithmComplianceDescriptor(
  uint32_t vertex_count,
  uint32_t triangle_count) {
  AlgorithmComplianceDescriptor descriptor{};
  descriptor.algorithm_name = kCorotatedCpuAlgorithmName;
  descriptor.cpu_available = true;
  descriptor.gpu_available = false;
  descriptor.data_contract.arrays_to_allocate = {
    AlgorithmBufferRequirement{"positions", vertex_count, static_cast<uint32_t>(sizeof(Vec3))},
    AlgorithmBufferRequirement{"total_velocities", vertex_count, static_cast<uint32_t>(sizeof(VelocityMatrix))}
  };
  descriptor.data_contract.temporary_registers_to_allocate = {
    AlgorithmBufferRequirement{"job_registers", vertex_count, static_cast<uint32_t>(sizeof(float) * 4u)}
  };
  descriptor.data_contract.temporary_caches_to_allocate = {
    AlgorithmBufferRequirement{"job_cache", triangle_count, static_cast<uint32_t>(sizeof(float) * 8u)}
  };
  descriptor.data_contract.filled_data_formats = {
    AlgorithmDataFormat{"positions", vertex_count, static_cast<uint32_t>(sizeof(Vec3))},
    AlgorithmDataFormat{"total_velocities", vertex_count, static_cast<uint32_t>(sizeof(VelocityMatrix))}
  };
  descriptor.data_contract.algorithm_required_formats = {
    AlgorithmDataFormat{"corotated_cpu_input", vertex_count, static_cast<uint32_t>(sizeof(Vec3))}
  };
  return descriptor;
}

}  // namespace algorithm
