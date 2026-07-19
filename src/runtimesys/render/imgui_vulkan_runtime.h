#pragma once

#if !defined(RUNTIME_SYSTEMS_LAYER_INTERNAL_BUILD)
#error "Do not include runtimesys/render/imgui_vulkan_runtime.h directly. Use runtimesys/runtime_environment.h."
#endif

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <functional>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include "runtimesys/render/render_preview_request.h"
#include "runtimesys/render/preview_renderer.h"
#include "runtimesys/debug_tool_recording.h"
#include "runtimesys/runtime_vk_context.h"

namespace runtimesys {

class ImGuiVulkanRuntime {
 public:
  using DrawCallback = std::function<void()>;

  bool Init(SDL_Window* window, const char* app_name = "debugTool");
  bool Tick(SDL_Window* window);
  void SetDrawCallback(DrawCallback callback);
  void SetSwapchainReadbackEnabled(bool enabled);
  void BeginDebugToolRecording();
  void EndDebugToolRecording();
  std::vector<DebugToolRecordedFrame> TakeDebugToolRecording();
  void SetRenderPreviewRequest(RenderPreviewRequest request);
  void SetRenderPreviewExtent(ImVec2 extent);
  void ClearVkRuntimeCaches();
  bool HasRenderPreviewTexture() const;
  bool ReadbackRenderPreviewTexture(std::vector<std::byte>* out_rgba, ImVec2* out_size);
  std::string RenderPreviewDebugSummary() const;
  ImTextureID RenderPreviewTextureId() const;
  ImVec2 RenderPreviewTextureSize() const;
  void Destroy();

 private:
  void SetupVulkan(const char* app_name, SDL_Window* window);
  void SetupVulkanWindow(SDL_Window* window, int width, int height);
  void CleanupVulkanWindow();
  void CleanupVulkan();
  void CreateSwapchainCaptureBuffers();
  void DestroySwapchainCaptureBuffers();
  void RecordSwapchainCapture(VkCommandBuffer command_buffer, VkImage image);
  void RecordPreviewImageCapture(
    VkCommandBuffer command_buffer,
    VkImage image,
    VkExtent2D extent,
    VkImageLayout image_layout);
  void RecordResultPreviewCapture(VkCommandBuffer command_buffer);
  void CreateResultPreviewCopy(VkExtent2D extent);
  void DestroyResultPreviewCopy();
  void RecordResultPreviewCopy(VkCommandBuffer command_buffer);
  void RemoveResultTexture();
  void RefreshResultTexture();
  bool FrameRender(ImDrawData* draw_data);
  void DrawDefaultOverlay(SDL_Window* window);
  static void CheckVkResult(VkResult err);

  VkAllocationCallbacks* allocator_{nullptr};
  VmaAllocator vma_allocator_{VK_NULL_HANDLE};
  VkInstance instance_{VK_NULL_HANDLE};
  VkPhysicalDevice physical_device_{VK_NULL_HANDLE};
  VkDevice device_{VK_NULL_HANDLE};
  VkQueue queue_{VK_NULL_HANDLE};
  std::vector<VkQueue> algorithm_queues_{};
  uint32_t queue_family_{UINT32_MAX};
  VkDescriptorPool descriptor_pool_{VK_NULL_HANDLE};
  VkPipelineCache pipeline_cache_{VK_NULL_HANDLE};
  VkSurfaceKHR surface_{VK_NULL_HANDLE};
  ImGui_ImplVulkanH_Window main_window_{};
  uint32_t min_image_count_{2};
  bool swapchain_readback_enabled_{false};
  struct SwapchainCaptureFrame {
    VkBuffer buffer{VK_NULL_HANDLE};
    VmaAllocation allocation{VK_NULL_HANDLE};
    VmaAllocationInfo allocation_info{};
    uint32_t width{0u};
    uint32_t height{0u};
    uint32_t bytes_per_pixel{0u};
    VkFormat format{VK_FORMAT_UNDEFINED};
    uint64_t tick_sequence{0u};
  };
  std::vector<SwapchainCaptureFrame> recorded_swapchain_frames_{};
  uint32_t swapchain_capture_bytes_per_pixel_{0u};
  bool debug_tool_recording_capable_{false};
  bool debug_tool_recording_active_{false};
  bool swapchain_rebuild_{false};
  bool initialized_{false};
  bool imgui_context_created_{false};
  bool sdl_backend_initialized_{false};
  bool vulkan_backend_initialized_{false};
  DrawCallback draw_callback_{};
  std::unique_ptr<PreviewRenderer> preview_renderer_{};
  RenderPreviewRequest pending_render_preview_request_{};
  bool has_pending_render_preview_request_{false};
  VkDescriptorSet result_texture_descriptor_set_{VK_NULL_HANDLE};
  RuntimeVkResultImage result_image_{};
  struct ResultPreviewCopy {
    VkImage image{VK_NULL_HANDLE};
    VmaAllocation allocation{VK_NULL_HANDLE};
    VkImageView view{VK_NULL_HANDLE};
    VkSampler sampler{VK_NULL_HANDLE};
    VkExtent2D extent{0u, 0u};
    bool initialized{false};
  } result_preview_copy_{};
  ImVec2 offscreen_surface_extent_{1024.0f, 1024.0f};
  uint64_t preview_frame_sequence_{0u};
  std::chrono::steady_clock::time_point last_preview_frame_end_{};
};

}  // namespace runtimesys
