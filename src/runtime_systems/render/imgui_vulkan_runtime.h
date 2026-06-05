#pragma once

#if !defined(RUNTIME_SYSTEMS_LAYER_INTERNAL_BUILD)
#error "Do not include runtime_systems/render/imgui_vulkan_runtime.h directly. Use runtime_systems/runtime_environment.h."
#endif

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <functional>
#include <string>

#include <vulkan/vulkan.h>

namespace runtime_systems {

class ImGuiVulkanRuntime {
 public:
  using DrawCallback = std::function<void()>;

  bool Init(SDL_Window* window, const char* app_name = "Agent Debug UI");
  bool Tick(SDL_Window* window);
  void SetDrawCallback(DrawCallback callback);
  void Destroy();

 private:
  void SetupVulkan(const char* app_name, SDL_Window* window);
  void SetupVulkanWindow(SDL_Window* window, int width, int height);
  void CleanupVulkanWindow();
  void CleanupVulkan();
  bool FrameRender(ImDrawData* draw_data);
  void DrawDefaultOverlay(SDL_Window* window);
  static void CheckVkResult(VkResult err);

  VkAllocationCallbacks* allocator_{nullptr};
  VkInstance instance_{VK_NULL_HANDLE};
  VkPhysicalDevice physical_device_{VK_NULL_HANDLE};
  VkDevice device_{VK_NULL_HANDLE};
  VkQueue queue_{VK_NULL_HANDLE};
  uint32_t queue_family_{UINT32_MAX};
  VkDescriptorPool descriptor_pool_{VK_NULL_HANDLE};
  VkPipelineCache pipeline_cache_{VK_NULL_HANDLE};
  VkSurfaceKHR surface_{VK_NULL_HANDLE};
  ImGui_ImplVulkanH_Window main_window_{};
  uint32_t min_image_count_{2};
  bool swapchain_rebuild_{false};
  bool initialized_{false};
  bool imgui_context_created_{false};
  bool sdl_backend_initialized_{false};
  bool vulkan_backend_initialized_{false};
  DrawCallback draw_callback_{};
};

}  // namespace runtime_systems
