#pragma once

#include <cstdint>
#include <mutex>

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace runtime_systems {

struct AlgorithmGpuExecutionContext {
  VkInstance instance{VK_NULL_HANDLE};
  VkPhysicalDevice physical_device{VK_NULL_HANDLE};
  VkDevice device{VK_NULL_HANDLE};
  VkQueue queue{VK_NULL_HANDLE};
  uint32_t queue_family{UINT32_MAX};
  VkDescriptorPool descriptor_pool{VK_NULL_HANDLE};
  VmaAllocator allocator{VK_NULL_HANDLE};

  bool valid() const {
    return instance != VK_NULL_HANDLE &&
      physical_device != VK_NULL_HANDLE &&
      device != VK_NULL_HANDLE &&
      queue != VK_NULL_HANDLE &&
      queue_family != UINT32_MAX &&
      descriptor_pool != VK_NULL_HANDLE &&
      allocator != VK_NULL_HANDLE;
  }
};

class AlgorithmGpuContextRegistry {
 public:
  static AlgorithmGpuContextRegistry& Instance();

  void Set(AlgorithmGpuExecutionContext context);
  AlgorithmGpuExecutionContext Snapshot() const;
  bool HasContext() const;
  void Clear();

 private:
  AlgorithmGpuContextRegistry() = default;

  mutable std::mutex mutex_{};
  AlgorithmGpuExecutionContext context_{};
};

}  // namespace runtime_systems
