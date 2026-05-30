#include "physics_convolution_gpu_algorithm_contract.h"

namespace algorithm {

AlgorithmComplianceDescriptor CreatePhysicsConvolutionGpuAlgorithmComplianceDescriptor(uint32_t element_count) {
  AlgorithmComplianceDescriptor descriptor{};
  descriptor.algorithm_name = kPhysicsConvolutionGpuAlgorithmName;
  descriptor.cpu_available = false;
  descriptor.gpu_available = true;
  descriptor.data_contract.arrays_to_allocate = {
    AlgorithmBufferRequirement{"shader_input", element_count, static_cast<uint32_t>(sizeof(float))}
  };
  descriptor.data_contract.temporary_registers_to_allocate = {
    AlgorithmBufferRequirement{"shader_registers", element_count, static_cast<uint32_t>(sizeof(float))}
  };
  descriptor.data_contract.temporary_caches_to_allocate = {
    AlgorithmBufferRequirement{"shader_cache", element_count, static_cast<uint32_t>(sizeof(float))}
  };
  descriptor.data_contract.filled_data_formats = {
    AlgorithmDataFormat{"shader_input", element_count, static_cast<uint32_t>(sizeof(float))}
  };
  descriptor.data_contract.algorithm_required_formats = {
    AlgorithmDataFormat{"physics_convolution_demo_input", element_count, static_cast<uint32_t>(sizeof(float))}
  };
  return descriptor;
}

}  // namespace algorithm
