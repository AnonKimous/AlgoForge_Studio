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

struct PreviewViewportPushConstants {
  float width{0.0f};
  float height{0.0f};
};

class PreviewRenderer {
 public:
  bool Init(
    VkInstance instance,
    VkPhysicalDevice physical_device,
    VkDevice device,
    VkDescriptorPool descriptor_pool,
    VmaAllocator allocator);
  void SetTargetExtent(VkExtent2D extent);
  void ApplyTargetExtent();
  void SetRequest(RenderPreviewRequest request);
  bool HasRequest() const;
  bool Record(VkCommandBuffer command_buffer);
  bool HasTexture() const;
  bool ReadbackTexture(std::vector<std::byte>* out_rgba, VkExtent2D* out_extent) const;
  std::string DebugSummary() const;
  VkDescriptorSet PreviewTextureDescriptorSet() const;
  VkExtent2D PreviewTextureExtent() const;
  void Destroy();

 private:
  struct PreviewTargetResource {
    VkImage image{VK_NULL_HANDLE};
    VmaAllocation allocation{VK_NULL_HANDLE};
    VkImageView view{VK_NULL_HANDLE};
    VkRenderPass render_pass{VK_NULL_HANDLE};
    VkFramebuffer framebuffer{VK_NULL_HANDLE};
    VkDescriptorSet descriptor_set{VK_NULL_HANDLE};
    VkExtent2D extent{1024u, 1024u};
    VkFormat format{VK_FORMAT_R8G8B8A8_UNORM};
  };

  struct PreviewBufferResource {
    VkBuffer buffer{VK_NULL_HANDLE};
    VmaAllocation allocation{VK_NULL_HANDLE};
    VmaAllocationInfo allocation_info{};
    VkDeviceSize size_bytes{0u};
  };

  struct PreviewReadbackResource {
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

  bool EnsureTarget();
  bool EnsurePipeline();
  bool CreatePipeline();
  bool CreateTarget();
  void DestroyPipeline();
  void DestroyTarget();
  void DestroyReadback();
  bool RecreateBuffers();
  bool UpdateBuffers();
  bool EnsureReadbackBuffer();
  bool UploadBuffer(
    PreviewBufferResource* buffer_resource,
    const RenderPreviewBuffer& source_buffer);

  VkInstance instance_{VK_NULL_HANDLE};
  VkPhysicalDevice physical_device_{VK_NULL_HANDLE};
  VkDevice device_{VK_NULL_HANDLE};
  VkDescriptorPool descriptor_pool_{VK_NULL_HANDLE};
  VmaAllocator allocator_{VK_NULL_HANDLE};

  RenderPreviewRequest request_{};
  PreviewTargetResource target_{};
  PreviewPipelineResource pipeline_{};
  PreviewReadbackResource readback_{};
  VkExtent2D requested_extent_{1024u, 1024u};
  bool target_extent_dirty_{false};
};

}  // namespace runtime_systems
