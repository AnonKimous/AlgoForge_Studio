#include "physics_convolution_gpu_algorithm_contract.h"

#include "data_reflection.h"

CreateDataReflectionInfo CreatePhysicsConvolutionGpuDataReflectionInfo(uint32_t element_count) {
  return CreateGpuShaderDataReflectionInfo(
    {
      ReflectionMemoryRequest{"shader_input", element_count, static_cast<uint32_t>(sizeof(PhysicsConvolutionGpuInputValue))}
    },
    {
      ReflectionMemoryRequest{"shader_registers", element_count, static_cast<uint32_t>(sizeof(PhysicsConvolutionGpuRegisterValue))}
    },
    {
      ReflectionMemoryRequest{"shader_cache", element_count, static_cast<uint32_t>(sizeof(PhysicsConvolutionGpuCacheValue))}
    },
    {
      ReflectionDataFormat{"shader_input", element_count, static_cast<uint32_t>(sizeof(PhysicsConvolutionGpuInputValue))}
    },
    {
      ReflectionDataFormat{"physics_convolution_demo_input", element_count, static_cast<uint32_t>(sizeof(PhysicsConvolutionGpuInputValue))}
    });
}
