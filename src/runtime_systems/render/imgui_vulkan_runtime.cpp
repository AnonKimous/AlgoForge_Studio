#include "imgui_vulkan_runtime.h"

#include "runtime_systems/algorithm_gpu_context.h"
#include "runtime_systems/job_system.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

namespace runtime_systems {

namespace {

#ifndef NDEBUG
#define DEBUG_TOOL_ASSERT(condition, message) assert((condition) && (message))
#else
#define DEBUG_TOOL_ASSERT(condition, message) ((void)0)
#endif

std::string VkErrorMessage(const char* prefix, VkResult err) {
  return std::string(prefix) + " failed: VkResult=" + std::to_string(static_cast<int>(err));
}

bool IsExtensionPresent(const std::vector<const char*>& extensions, const char* extension) {
  return std::find(extensions.begin(), extensions.end(), extension) != extensions.end();
}

}  // namespace

void ImGuiVulkanRuntime::CheckVkResult(VkResult err) {
  if (err == VK_SUCCESS) {
    return;
  }
  throw std::runtime_error(VkErrorMessage("Vulkan call", err));
}

void ImGuiVulkanRuntime::SetupVulkan(const char* app_name, SDL_Window* window) {
  uint32_t extension_count = 0;
  const char* const* sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&extension_count);
  if (!sdl_extensions || extension_count == 0) {
    throw std::runtime_error("SDL_Vulkan_GetInstanceExtensions returned no extensions");
  }

  std::vector<const char*> instance_extensions;
  instance_extensions.reserve(extension_count + 1);
  for (uint32_t i = 0; i < extension_count; ++i) {
    instance_extensions.push_back(sdl_extensions[i]);
  }
  if (!IsExtensionPresent(instance_extensions, VK_KHR_SURFACE_EXTENSION_NAME)) {
    instance_extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
  }

  VkApplicationInfo app_info{};
  app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app_info.pApplicationName = app_name ? app_name : "debugTool";
  app_info.apiVersion = VK_API_VERSION_1_3;

  VkInstanceCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  create_info.pApplicationInfo = &app_info;
  create_info.enabledExtensionCount = static_cast<uint32_t>(instance_extensions.size());
  create_info.ppEnabledExtensionNames = instance_extensions.data();

  CheckVkResult(vkCreateInstance(&create_info, allocator_, &instance_));

  CheckVkResult(SDL_Vulkan_CreateSurface(window, instance_, allocator_, &surface_) ? VK_SUCCESS : VK_ERROR_INITIALIZATION_FAILED);

  uint32_t physical_device_count = 0;
  CheckVkResult(vkEnumeratePhysicalDevices(instance_, &physical_device_count, nullptr));
  if (physical_device_count == 0) {
    throw std::runtime_error("No Vulkan physical devices found");
  }

  std::vector<VkPhysicalDevice> physical_devices(physical_device_count);
  CheckVkResult(vkEnumeratePhysicalDevices(instance_, &physical_device_count, physical_devices.data()));

  for (VkPhysicalDevice device : physical_devices) {
    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, nullptr);
    std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, queue_families.data());

    for (uint32_t family = 0; family < queue_family_count; ++family) {
      VkBool32 present_supported = VK_FALSE;
      vkGetPhysicalDeviceSurfaceSupportKHR(device, family, surface_, &present_supported);
      const bool graphics_supported = (queue_families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
      if (graphics_supported && present_supported) {
        physical_device_ = device;
        queue_family_ = family;
        break;
      }
    }

    if (physical_device_ != VK_NULL_HANDLE) {
      break;
    }
  }

  if (physical_device_ == VK_NULL_HANDLE || queue_family_ == UINT32_MAX) {
    throw std::runtime_error("No suitable Vulkan device queue family found for ImGui");
  }

  const float queue_priority = 1.0f;
  VkDeviceQueueCreateInfo queue_create_info{};
  queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queue_create_info.queueFamilyIndex = queue_family_;
  queue_create_info.queueCount = 1;
  queue_create_info.pQueuePriorities = &queue_priority;

  VkPhysicalDeviceFeatures available_features{};
  vkGetPhysicalDeviceFeatures(physical_device_, &available_features);
  if (!available_features.vertexPipelineStoresAndAtomics) {
    throw std::runtime_error(
      "Selected Vulkan device does not support vertexPipelineStoresAndAtomics, so GPU tick cannot write back from vertex shader.");
  }

  VkPhysicalDeviceFeatures enabled_features{};
  enabled_features.vertexPipelineStoresAndAtomics = VK_TRUE;

  const char* device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  VkDeviceCreateInfo device_create_info{};
  device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  device_create_info.queueCreateInfoCount = 1;
  device_create_info.pQueueCreateInfos = &queue_create_info;
  device_create_info.pEnabledFeatures = &enabled_features;
  device_create_info.enabledExtensionCount = 1;
  device_create_info.ppEnabledExtensionNames = device_extensions;

  CheckVkResult(vkCreateDevice(physical_device_, &device_create_info, allocator_, &device_));
  vkGetDeviceQueue(device_, queue_family_, 0, &queue_);

  VmaAllocatorCreateInfo vma_create_info{};
  vma_create_info.instance = instance_;
  vma_create_info.physicalDevice = physical_device_;
  vma_create_info.device = device_;
  vma_create_info.vulkanApiVersion = VK_API_VERSION_1_3;
  CheckVkResult(vmaCreateAllocator(&vma_create_info, &vma_allocator_));

  VkDescriptorPoolSize pool_sizes[] = {
    {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 8},
    {VK_DESCRIPTOR_TYPE_SAMPLER, 2},
    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 64},
  };
  VkDescriptorPoolCreateInfo pool_info{};
  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  pool_info.maxSets = 0;
  for (const VkDescriptorPoolSize& pool_size : pool_sizes) {
    pool_info.maxSets += pool_size.descriptorCount;
  }
  pool_info.poolSizeCount = static_cast<uint32_t>(sizeof(pool_sizes) / sizeof(pool_sizes[0]));
  pool_info.pPoolSizes = pool_sizes;
  CheckVkResult(vkCreateDescriptorPool(device_, &pool_info, allocator_, &descriptor_pool_));

  AlgorithmGpuContextRegistry::Instance().Set(AlgorithmGpuExecutionContext{
    .instance = instance_,
    .physical_device = physical_device_,
    .device = device_,
    .queue = queue_,
    .queue_family = queue_family_,
    .descriptor_pool = descriptor_pool_,
    .allocator = vma_allocator_,
  });
}

void ImGuiVulkanRuntime::SetupVulkanWindow(SDL_Window* window, int width, int height) {
  VkBool32 present_supported = VK_FALSE;
  vkGetPhysicalDeviceSurfaceSupportKHR(physical_device_, queue_family_, surface_, &present_supported);
  if (present_supported != VK_TRUE) {
    throw std::runtime_error("Selected Vulkan queue family does not support presentation");
  }

  const VkFormat request_formats[] = {
    VK_FORMAT_B8G8R8A8_UNORM,
    VK_FORMAT_R8G8B8A8_UNORM,
    VK_FORMAT_B8G8R8_UNORM,
    VK_FORMAT_R8G8B8_UNORM,
  };
  const VkColorSpaceKHR request_color_space = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
  main_window_.Surface = surface_;
  main_window_.SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
    physical_device_,
    main_window_.Surface,
    request_formats,
    static_cast<int>(sizeof(request_formats) / sizeof(request_formats[0])),
    request_color_space);

  const VkPresentModeKHR present_modes[] = {VK_PRESENT_MODE_FIFO_KHR};
  main_window_.PresentMode = ImGui_ImplVulkanH_SelectPresentMode(
    physical_device_,
    main_window_.Surface,
    present_modes,
    static_cast<int>(sizeof(present_modes) / sizeof(present_modes[0])));

  ImGui_ImplVulkanH_CreateOrResizeWindow(
    instance_,
    physical_device_,
    device_,
    &main_window_,
    queue_family_,
    allocator_,
    width,
    height,
    min_image_count_,
    0);
}

void ImGuiVulkanRuntime::CleanupVulkanWindow() {
  if (main_window_.Surface != VK_NULL_HANDLE) {
    ImGui_ImplVulkanH_DestroyWindow(instance_, device_, &main_window_, allocator_);
    vkDestroySurfaceKHR(instance_, main_window_.Surface, allocator_);
    main_window_.Surface = VK_NULL_HANDLE;
  }
}

void ImGuiVulkanRuntime::CleanupVulkan() {
  AlgorithmGpuContextRegistry::Instance().Clear();
  if (vma_allocator_ != VK_NULL_HANDLE) {
    vmaDestroyAllocator(vma_allocator_);
    vma_allocator_ = VK_NULL_HANDLE;
  }
  if (descriptor_pool_ != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(device_, descriptor_pool_, allocator_);
    descriptor_pool_ = VK_NULL_HANDLE;
  }
  if (device_ != VK_NULL_HANDLE) {
    vkDestroyDevice(device_, allocator_);
    device_ = VK_NULL_HANDLE;
  }
  if (instance_ != VK_NULL_HANDLE) {
    vkDestroyInstance(instance_, allocator_);
    instance_ = VK_NULL_HANDLE;
  }
  queue_ = VK_NULL_HANDLE;
  physical_device_ = VK_NULL_HANDLE;
  queue_family_ = UINT32_MAX;
}

bool ImGuiVulkanRuntime::FrameRender(ImDrawData* draw_data) {
  VkSemaphore image_acquired_semaphore = main_window_.FrameSemaphores[main_window_.SemaphoreIndex].ImageAcquiredSemaphore;
  VkSemaphore render_complete_semaphore = main_window_.FrameSemaphores[main_window_.SemaphoreIndex].RenderCompleteSemaphore;
  VkResult err = vkAcquireNextImageKHR(device_, main_window_.Swapchain, UINT64_MAX, image_acquired_semaphore, VK_NULL_HANDLE, &main_window_.FrameIndex);
  if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
    swapchain_rebuild_ = true;
  }
  if (err == VK_ERROR_OUT_OF_DATE_KHR) {
    return false;
  }
  if (err != VK_SUBOPTIMAL_KHR) {
    CheckVkResult(err);
  }

  ImGui_ImplVulkanH_Frame* fd = &main_window_.Frames[main_window_.FrameIndex];
  CheckVkResult(vkWaitForFences(device_, 1, &fd->Fence, VK_TRUE, UINT64_MAX));
  CheckVkResult(vkResetFences(device_, 1, &fd->Fence));

  CheckVkResult(vkResetCommandPool(device_, fd->CommandPool, 0));
  VkCommandBufferBeginInfo begin_info{};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  CheckVkResult(vkBeginCommandBuffer(fd->CommandBuffer, &begin_info));

  if (preview_renderer_) {
    if (preview_renderer_->HasRequest()) {
      const bool preview_recorded = preview_renderer_->Record(fd->CommandBuffer);
      DEBUG_TOOL_ASSERT(preview_recorded, "Preview request was accepted but could not be rendered.");
    }
  }

  VkRenderPassBeginInfo render_pass_begin_info{};
  render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  render_pass_begin_info.renderPass = main_window_.RenderPass;
  render_pass_begin_info.framebuffer = fd->Framebuffer;
  render_pass_begin_info.renderArea.extent.width = main_window_.Width;
  render_pass_begin_info.renderArea.extent.height = main_window_.Height;
  render_pass_begin_info.clearValueCount = 1;
  render_pass_begin_info.pClearValues = &main_window_.ClearValue;
  vkCmdBeginRenderPass(fd->CommandBuffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

  ImGui_ImplVulkan_RenderDrawData(draw_data, fd->CommandBuffer);

  vkCmdEndRenderPass(fd->CommandBuffer);

  VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo submit_info{};
  submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit_info.waitSemaphoreCount = 1;
  submit_info.pWaitSemaphores = &image_acquired_semaphore;
  submit_info.pWaitDstStageMask = &wait_stage;
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &fd->CommandBuffer;
  submit_info.signalSemaphoreCount = 1;
  submit_info.pSignalSemaphores = &render_complete_semaphore;

  CheckVkResult(vkEndCommandBuffer(fd->CommandBuffer));
  CheckVkResult(vkQueueSubmit(queue_, 1, &submit_info, fd->Fence));
  return true;
}

void ImGuiVulkanRuntime::DrawDefaultOverlay(SDL_Window* window) {
  int width = 0;
  int height = 0;
  SDL_GetWindowSizeInPixels(window, &width, &height);

  ImGui::Begin("Runtime Debug");
  ImGui::Text("Window: %dx%d", width, height);
  ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
  float mouse_x = 0.0f;
  float mouse_y = 0.0f;
  SDL_GetMouseState(&mouse_x, &mouse_y);
  ImGui::Text("Mouse: %d, %d", static_cast<int>(mouse_x), static_cast<int>(mouse_y));
  ImGui::Text("ImGui backend: SDL3 + Vulkan");
  ImGui::End();
}

bool ImGuiVulkanRuntime::Init(SDL_Window* window, const char* app_name) {
  if (!window) {
    return false;
  }
  if (initialized_) {
    return true;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  imgui_context_created_ = true;
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  ImGui::StyleColorsDark();

  SetupVulkan(app_name, window);

  if (!ImGui_ImplSDL3_InitForVulkan(window)) {
    Destroy();
    return false;
  }
  sdl_backend_initialized_ = true;

  int width = 0;
  int height = 0;
  SDL_GetWindowSizeInPixels(window, &width, &height);
  SetupVulkanWindow(window, width, height);

  ImGui_ImplVulkan_InitInfo init_info{};
  init_info.ApiVersion = VK_API_VERSION_1_3;
  init_info.Instance = instance_;
  init_info.PhysicalDevice = physical_device_;
  init_info.Device = device_;
  init_info.QueueFamily = queue_family_;
  init_info.Queue = queue_;
  init_info.DescriptorPool = descriptor_pool_;
  init_info.MinImageCount = min_image_count_;
  init_info.ImageCount = main_window_.ImageCount;
  init_info.Allocator = allocator_;
  init_info.PipelineInfoMain.RenderPass = main_window_.RenderPass;
  init_info.PipelineInfoMain.Subpass = 0;
  init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  init_info.CheckVkResultFn = CheckVkResult;
  if (!ImGui_ImplVulkan_Init(&init_info)) {
    Destroy();
    return false;
  }
  vulkan_backend_initialized_ = true;

  if (!preview_renderer_) {
    preview_renderer_ = std::make_unique<PreviewRenderer>();
  }
  if (!preview_renderer_->Init(instance_, physical_device_, device_, descriptor_pool_, vma_allocator_)) {
    Destroy();
    return false;
  }
  if (has_pending_render_preview_request_) {
    preview_renderer_->SetRequest(pending_render_preview_request_);
    DEBUG_TOOL_ASSERT(
      !pending_render_preview_request_.valid || preview_renderer_->HasRequest(),
      "Pending render preview request was not accepted during runtime initialization.");
    if (pending_render_preview_request_.valid && preview_renderer_->HasRequest()) {
      has_pending_render_preview_request_ = false;
    }
  }

  initialized_ = true;
  return true;
}

bool ImGuiVulkanRuntime::HasRenderPreviewTexture() const {
  return preview_renderer_ ? preview_renderer_->HasTexture() : false;
}

std::string ImGuiVulkanRuntime::RenderPreviewDebugSummary() const {
  if (!preview_renderer_) {
    return "preview_renderer=uninitialized";
  }
  return preview_renderer_->DebugSummary();
}

ImTextureID ImGuiVulkanRuntime::RenderPreviewTextureId() const {
  return preview_renderer_
    ? (ImTextureID)(intptr_t)preview_renderer_->PreviewTextureDescriptorSet()
    : ImTextureID{};
}

ImVec2 ImGuiVulkanRuntime::RenderPreviewTextureSize() const {
  if (!preview_renderer_) {
    return ImVec2{};
  }
  const VkExtent2D extent = preview_renderer_->PreviewTextureExtent();
  return ImVec2(static_cast<float>(extent.width), static_cast<float>(extent.height));
}

void ImGuiVulkanRuntime::SetDrawCallback(DrawCallback callback) {
  draw_callback_ = std::move(callback);
}

void ImGuiVulkanRuntime::SetRenderPreviewRequest(RenderPreviewRequest request) {
  pending_render_preview_request_ = request;
  has_pending_render_preview_request_ = request.valid;

  if (!preview_renderer_) {
    DEBUG_TOOL_ASSERT(
      !request.valid,
      "A valid render preview request arrived before the preview renderer was initialized.");
    return;
  }

  preview_renderer_->SetRequest(std::move(request));
  DEBUG_TOOL_ASSERT(
    !pending_render_preview_request_.valid || preview_renderer_->HasRequest(),
    "Render preview request was not accepted by the preview renderer.");
  if (preview_renderer_->HasRequest()) {
    has_pending_render_preview_request_ = false;
  }
}

void ImGuiVulkanRuntime::SetRenderPreviewExtent(ImVec2 extent) {
  if (preview_renderer_) {
    preview_renderer_->SetTargetExtent(VkExtent2D{
      static_cast<uint32_t>(extent.x),
      static_cast<uint32_t>(extent.y),
    });
  }
}

void ImGuiVulkanRuntime::ClearGpuRuntimeCaches() {
  if (device_ != VK_NULL_HANDLE) {
    CheckVkResult(vkDeviceWaitIdle(device_));
  }
  job_gpu::Clear();
}

bool ImGuiVulkanRuntime::Tick(SDL_Window* window) {
  if (!initialized_ || !window) {
    return false;
  }

  if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
    SDL_Delay(10);
    return true;
  }

  int fb_width = 0;
  int fb_height = 0;
  SDL_GetWindowSizeInPixels(window, &fb_width, &fb_height);
  if (fb_width > 0 && fb_height > 0 && (swapchain_rebuild_ || main_window_.Width != fb_width || main_window_.Height != fb_height)) {
    CheckVkResult(vkDeviceWaitIdle(device_));
    ImGui_ImplVulkan_SetMinImageCount(min_image_count_);
    ImGui_ImplVulkanH_CreateOrResizeWindow(
      instance_,
      physical_device_,
      device_,
      &main_window_,
      queue_family_,
      allocator_,
      fb_width,
      fb_height,
      min_image_count_,
      0);
    main_window_.FrameIndex = 0;
    main_window_.SemaphoreIndex = 0;
    swapchain_rebuild_ = false;
  }

  if (preview_renderer_) {
    preview_renderer_->ApplyTargetExtent();
  }

  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();

  if (draw_callback_) {
    draw_callback_();
  } else {
    DrawDefaultOverlay(window);
  }

  ImGui::Render();
  ImDrawData* draw_data = ImGui::GetDrawData();
  const bool minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
  main_window_.ClearValue.color.float32[0] = 0.10f;
  main_window_.ClearValue.color.float32[1] = 0.10f;
  main_window_.ClearValue.color.float32[2] = 0.12f;
  main_window_.ClearValue.color.float32[3] = 1.0f;
  if (!minimized) {
    if (FrameRender(draw_data)) {
      VkSemaphore render_complete_semaphore = main_window_.FrameSemaphores[main_window_.SemaphoreIndex].RenderCompleteSemaphore;
      VkPresentInfoKHR present_info{};
      present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
      present_info.waitSemaphoreCount = 1;
      present_info.pWaitSemaphores = &render_complete_semaphore;
      present_info.swapchainCount = 1;
      present_info.pSwapchains = &main_window_.Swapchain;
      present_info.pImageIndices = &main_window_.FrameIndex;
      VkResult err = vkQueuePresentKHR(queue_, &present_info);
      if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
        swapchain_rebuild_ = true;
      } else {
        CheckVkResult(err);
      }
      main_window_.SemaphoreIndex = (main_window_.SemaphoreIndex + 1) % main_window_.SemaphoreCount;
    }
  }

  return true;
}

void ImGuiVulkanRuntime::Destroy() {
  if (!initialized_ && instance_ == VK_NULL_HANDLE && device_ == VK_NULL_HANDLE) {
    return;
  }

  try {
    ClearGpuRuntimeCaches();
  } catch (...) {
    // Shutdown should keep going even if the device was already lost.
  }

  if (preview_renderer_) {
    preview_renderer_->Destroy();
    preview_renderer_.reset();
  }

  CleanupVulkanWindow();

  if (vulkan_backend_initialized_) {
    ImGui_ImplVulkan_Shutdown();
    vulkan_backend_initialized_ = false;
  }
  if (sdl_backend_initialized_) {
    ImGui_ImplSDL3_Shutdown();
    sdl_backend_initialized_ = false;
  }
  if (imgui_context_created_) {
    ImGui::DestroyContext();
    imgui_context_created_ = false;
  }
  CleanupVulkan();

  initialized_ = false;
  swapchain_rebuild_ = false;
  draw_callback_ = {};
}

}  // namespace runtime_systems
