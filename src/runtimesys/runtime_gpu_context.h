#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace runtimesys {

struct RuntimeVkExecutionContext {
  VkInstance instance{VK_NULL_HANDLE};
  VkPhysicalDevice physical_device{VK_NULL_HANDLE};
  VkDevice device{VK_NULL_HANDLE};
  VkQueue queue{VK_NULL_HANDLE};
  uint32_t queue_index{UINT32_MAX};
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

struct RuntimeVkResultImage {
  VkImage image{VK_NULL_HANDLE};
  VkImageView view{VK_NULL_HANDLE};
  VkSampler sampler{VK_NULL_HANDLE};
  VkImageLayout layout{VK_IMAGE_LAYOUT_UNDEFINED};
  VkExtent2D extent{0u, 0u};
  uint64_t generation{0u};

  bool valid() const {
    return view != VK_NULL_HANDLE && sampler != VK_NULL_HANDLE &&
      layout != VK_IMAGE_LAYOUT_UNDEFINED && extent.width != 0u && extent.height != 0u;
  }
};

class RuntimeVkContextRegistry {
 public:
  static RuntimeVkContextRegistry& Instance();

  void Set(RuntimeVkExecutionContext context);
  void SetAlgorithmQueues(
    std::vector<VkQueue> queues,
    uint32_t first_queue_index);
  RuntimeVkExecutionContext Snapshot(const void* execution_key = nullptr) const;
  void PublishExecutionProgress(const void* execution_key);
  uint64_t SnapshotExecutionProgress(const void* execution_key) const;
  void PublishResultImage(const void* execution_key, RuntimeVkResultImage image);
  RuntimeVkResultImage SnapshotResultImage(const void* execution_key) const;
  void ClearTransientState();
  bool HasContext() const;
  void Clear();

 private:
  RuntimeVkContextRegistry() = default;

  mutable std::mutex mutex_{};
  RuntimeVkExecutionContext context_{};
  std::vector<VkQueue> algorithm_queues_{};
  uint32_t algorithm_queue_first_index_{1u};
  mutable std::unordered_map<const void*, uint32_t> queue_assignments_{};
  mutable uint32_t next_algorithm_queue_index_{0u};
  std::unordered_map<const void*, uint64_t> execution_progress_{};
  std::unordered_map<const void*, RuntimeVkResultImage> result_images_{};
};

}  // namespace runtimesys
