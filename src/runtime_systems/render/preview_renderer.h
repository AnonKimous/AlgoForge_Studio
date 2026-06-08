#pragma once

#if !defined(RUNTIME_SYSTEMS_LAYER_INTERNAL_BUILD)
#error "Do not include runtime_systems/render/preview_renderer.h directly. Use runtime_systems/runtime_environment.h."
#endif

#include "runtime_systems/render/render_preview_request.h"

#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <string>

namespace runtime_systems {

class PreviewRenderer {
 public:
  bool Init(
    VkInstance instance,
    VkPhysicalDevice physical_device,
    VkDevice device,
    VkDescriptorPool descriptor_pool,
    VkRenderPass render_pass,
    VmaAllocator allocator);
  void SetRequest(RenderPreviewRequest request);
  bool Record(VkCommandBuffer command_buffer, uint32_t width, uint32_t height);
  void Destroy();

 private:
  struct PreviewBufferResource {
    VkBuffer buffer{VK_NULL_HANDLE};
    VmaAllocation allocation{VK_NULL_HANDLE};
    VmaAllocationInfo allocation_info{};
    VkDeviceSize size_bytes{0u};
  };

  struct PreviewPipelineResource {
    VkDescriptorSetLayout descriptor_set_layout{VK_NULL_HANDLE};
    VkPipelineLayout pipeline_layout{VK_NULL_HANDLE};
    VkPipeline pipeline{VK_NULL_HANDLE};
    VkDescriptorSet descriptor_set{VK_NULL_HANDLE};
    std::vector<PreviewBufferResource> buffers;
    std::string shader_key;
    uint32_t buffer_binding_count{0u};
    uint32_t instance_count{0u};
    bool valid{false};
  };

  static void CheckVkResult(VkResult err);
  static std::string VkErrorMessage(const char* prefix, VkResult err);
  static std::vector<std::byte> ReadBinaryFile(const std::string& path);
  static std::string ResolveShaderBinaryPath(const std::string& path);

  bool EnsurePipeline();
  bool CreatePipeline();
  void DestroyPipeline();
  bool RecreateBuffers();
  bool UpdateBuffers();
  bool UploadBuffer(
    PreviewBufferResource* buffer_resource,
    const RenderPreviewBuffer& source_buffer);

  VkInstance instance_{VK_NULL_HANDLE};
  VkPhysicalDevice physical_device_{VK_NULL_HANDLE};
  VkDevice device_{VK_NULL_HANDLE};
  VkDescriptorPool descriptor_pool_{VK_NULL_HANDLE};
  VkRenderPass render_pass_{VK_NULL_HANDLE};
  VmaAllocator allocator_{VK_NULL_HANDLE};

  RenderPreviewRequest request_{};
  PreviewPipelineResource pipeline_{};
};

}  // namespace runtime_systems
