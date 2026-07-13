#include "imgui_vulkan_runtime.h"

#include "algorithm_catalog/algorithm_library_paths.h"
#include "runtime_systems/runtime_vk_context.h"
#include "runtime_systems/job_system.h"
#include "runtime_systems/runtime_environment.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

namespace runtime_systems {

namespace {

#ifndef ALGOFORGE_VERBOSE_RUNTIME_LOGGING
#define ALGOFORGE_VERBOSE_RUNTIME_LOGGING 0
#endif

#ifndef NDEBUG
#define DEBUG_TOOL_ASSERT(condition, message) do { \
  if (!(condition)) { \
    std::cerr << (message) << '\n'; \
    assert((condition) && (message)); \
  } \
} while (false)
#else
#define DEBUG_TOOL_ASSERT(condition, message) ((void)0)
#endif

std::string VkErrorMessage(const char* prefix, VkResult err) {
  return std::string(prefix) + " failed: VkResult=" + std::to_string(static_cast<int>(err));
}

bool IsExtensionPresent(const std::vector<const char*>& extensions, const char* extension) {
  return std::find(extensions.begin(), extensions.end(), extension) != extensions.end();
}

void AppendImguiVulkanProbe(const std::string& line) {
#if ALGOFORGE_VERBOSE_RUNTIME_LOGGING
  const std::filesystem::path probe_path =
    algorithm::library_paths::ResolveAlgorithmLibraryRuntimePipelineDebugInfoRoot() / "runtime_init_probe.log";
  std::error_code ec;
  std::filesystem::create_directories(probe_path.parent_path(), ec);
  std::ofstream file(probe_path, std::ios::binary | std::ios::app);
  if (file) {
    file << line << '\n';
  }
#else
  (void)line;
#endif
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
      if (graphics_supported && present_supported && queue_families[family].queueCount >= 2u) {
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

  uint32_t selected_queue_family_count = 0u;
  vkGetPhysicalDeviceQueueFamilyProperties(
    physical_device_,
    &selected_queue_family_count,
    nullptr);
  std::vector<VkQueueFamilyProperties> selected_queue_families(selected_queue_family_count);
  vkGetPhysicalDeviceQueueFamilyProperties(
    physical_device_,
    &selected_queue_family_count,
    selected_queue_families.data());
  const uint32_t queue_count = selected_queue_families[queue_family_].queueCount;
  if (queue_count < 2u) {
    throw std::runtime_error("The Vulkan present queue family must expose a second queue for algorithms");
  }
  const std::vector<float> queue_priorities(queue_count, 1.0f);
  VkDeviceQueueCreateInfo queue_create_info{};
  queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queue_create_info.queueFamilyIndex = queue_family_;
  queue_create_info.queueCount = queue_count;
  queue_create_info.pQueuePriorities = queue_priorities.data();

  VkPhysicalDeviceFeatures available_features{};
  vkGetPhysicalDeviceFeatures(physical_device_, &available_features);
  if (!available_features.vertexPipelineStoresAndAtomics) {
    throw std::runtime_error(
      "Selected Vulkan device does not support vertexPipelineStoresAndAtomics, so VK tick cannot write back from vertex shader.");
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
  algorithm_queues_.clear();
  algorithm_queues_.reserve(queue_count - 1u);
  for (uint32_t queue_index = 1u; queue_index < queue_count; ++queue_index) {
    VkQueue algorithm_queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device_, queue_family_, queue_index, &algorithm_queue);
    algorithm_queues_.push_back(algorithm_queue);
  }

  VmaAllocatorCreateInfo vma_create_info{};
  vma_create_info.instance = instance_;
  vma_create_info.physicalDevice = physical_device_;
  vma_create_info.device = device_;
  vma_create_info.vulkanApiVersion = VK_API_VERSION_1_3;
  CheckVkResult(vmaCreateAllocator(&vma_create_info, &vma_allocator_));

  VkDescriptorPoolSize pool_sizes[] = {
    {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 64},
    {VK_DESCRIPTOR_TYPE_SAMPLER, 16},
    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 256},
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

  RuntimeVkContextRegistry::Instance().Set(RuntimeVkExecutionContext{
    .instance = instance_,
    .physical_device = physical_device_,
    .device = device_,
    .queue = queue_,
    .queue_index = 0u,
    .queue_family = queue_family_,
    .descriptor_pool = descriptor_pool_,
    .allocator = vma_allocator_,
  });
  RuntimeVkContextRegistry::Instance().SetAlgorithmQueues(algorithm_queues_);
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
  RuntimeVkContextRegistry::Instance().Clear();
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
  algorithm_queues_.clear();
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

  if (preview_renderer_ && result_texture_descriptor_set_ == VK_NULL_HANDLE) {
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
  if (initialized_) {
    return true;
  }

  AppendImguiVulkanProbe("imgui_runtime.init.begin");
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  imgui_context_created_ = true;
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  ImGui::StyleColorsDark();

  AppendImguiVulkanProbe("imgui_runtime.init.setup_vulkan.begin");
  SetupVulkan(app_name, window);
  AppendImguiVulkanProbe("imgui_runtime.init.setup_vulkan.end");

  AppendImguiVulkanProbe("imgui_runtime.init.sdl_backend.begin");
  if (!ImGui_ImplSDL3_InitForVulkan(window)) {
    AppendImguiVulkanProbe("imgui_runtime.init.sdl_backend.failed");
    Destroy();
    return false;
  }
  sdl_backend_initialized_ = true;
  AppendImguiVulkanProbe("imgui_runtime.init.sdl_backend.end");

  int width = 0;
  int height = 0;
  SDL_GetWindowSizeInPixels(window, &width, &height);
  AppendImguiVulkanProbe("imgui_runtime.init.setup_window.begin");
  SetupVulkanWindow(window, width, height);
  AppendImguiVulkanProbe("imgui_runtime.init.setup_window.end");

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
  AppendImguiVulkanProbe("imgui_runtime.init.vulkan_backend.begin");
  if (!ImGui_ImplVulkan_Init(&init_info)) {
    AppendImguiVulkanProbe("imgui_runtime.init.vulkan_backend.failed");
    Destroy();
    return false;
  }
  vulkan_backend_initialized_ = true;
  AppendImguiVulkanProbe("imgui_runtime.init.vulkan_backend.end");

  if (!preview_renderer_) {
    preview_renderer_ = std::make_unique<PreviewRenderer>();
  }
  AppendImguiVulkanProbe("imgui_runtime.init.preview_renderer.begin");
  if (!preview_renderer_->Init(instance_, physical_device_, device_, descriptor_pool_, vma_allocator_)) {
    AppendImguiVulkanProbe("imgui_runtime.init.preview_renderer.failed");
    Destroy();
    return false;
  }
  AppendImguiVulkanProbe("imgui_runtime.init.preview_renderer.end");
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
  AppendImguiVulkanProbe("imgui_runtime.init.end");
  return true;
}

bool ImGuiVulkanRuntime::HasRenderPreviewTexture() const {
  return result_texture_descriptor_set_ != VK_NULL_HANDLE ||
    (preview_renderer_ ? preview_renderer_->HasTexture() : false);
}

bool ImGuiVulkanRuntime::ReadbackRenderPreviewTexture(
  std::vector<std::byte>* out_rgba,
  ImVec2* out_size) {
  if (!out_rgba) {
    return false;
  }
  if (device_ == VK_NULL_HANDLE) {
    return false;
  }

  CheckVkResult(vkDeviceWaitIdle(device_));

  if (result_image_.valid() && result_image_.image != VK_NULL_HANDLE) {
    const VkExtent2D extent = result_image_.extent;
    const VkDeviceSize required_size =
      static_cast<VkDeviceSize>(extent.width) *
      static_cast<VkDeviceSize>(extent.height) * 4u;

    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VmaAllocation staging_allocation = VK_NULL_HANDLE;
    VmaAllocationInfo staging_allocation_info{};
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = required_size;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo allocation_info{};
    allocation_info.usage = VMA_MEMORY_USAGE_AUTO;
    allocation_info.flags =
      VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
      VMA_ALLOCATION_CREATE_MAPPED_BIT;
    CheckVkResult(vmaCreateBuffer(
      vma_allocator_,
      &buffer_info,
      &allocation_info,
      &staging_buffer,
      &staging_allocation,
      &staging_allocation_info));

    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo command_pool_info{};
    command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    command_pool_info.queueFamilyIndex = queue_family_;
    CheckVkResult(vkCreateCommandPool(device_, &command_pool_info, nullptr, &command_pool));

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo command_buffer_info{};
    command_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_buffer_info.commandPool = command_pool;
    command_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_buffer_info.commandBufferCount = 1u;
    CheckVkResult(vkAllocateCommandBuffers(device_, &command_buffer_info, &command_buffer));

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CheckVkResult(vkBeginCommandBuffer(command_buffer, &begin_info));

    VkImageMemoryBarrier to_transfer{};
    to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_transfer.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    to_transfer.oldLayout = result_image_.layout;
    to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_transfer.image = result_image_.image;
    to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    to_transfer.subresourceRange.levelCount = 1u;
    to_transfer.subresourceRange.layerCount = 1u;
    vkCmdPipelineBarrier(
      command_buffer,
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      0,
      0,
      nullptr,
      0,
      nullptr,
      1,
      &to_transfer);

    VkBufferImageCopy copy_region{};
    copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy_region.imageSubresource.layerCount = 1u;
    copy_region.imageExtent = {extent.width, extent.height, 1u};
    vkCmdCopyImageToBuffer(
      command_buffer,
      result_image_.image,
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      staging_buffer,
      1u,
      &copy_region);

    VkImageMemoryBarrier to_shader_read = to_transfer;
    to_shader_read.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    to_shader_read.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    to_shader_read.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    to_shader_read.newLayout = result_image_.layout;
    vkCmdPipelineBarrier(
      command_buffer,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      0,
      0,
      nullptr,
      0,
      nullptr,
      1,
      &to_shader_read);
    CheckVkResult(vkEndCommandBuffer(command_buffer));

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1u;
    submit_info.pCommandBuffers = &command_buffer;
    CheckVkResult(vkQueueSubmit(queue_, 1u, &submit_info, VK_NULL_HANDLE));
    CheckVkResult(vkQueueWaitIdle(queue_));
    CheckVkResult(vmaInvalidateAllocation(
      vma_allocator_,
      staging_allocation,
      0u,
      required_size));
    const auto* mapped_bytes =
      static_cast<const std::byte*>(staging_allocation_info.pMappedData);
    out_rgba->assign(mapped_bytes, mapped_bytes + required_size);

    vkDestroyCommandPool(device_, command_pool, nullptr);
    vmaDestroyBuffer(vma_allocator_, staging_buffer, staging_allocation);
    if (out_size) {
      *out_size = ImVec2(
        static_cast<float>(extent.width),
        static_cast<float>(extent.height));
    }
    return true;
  }

  if (!preview_renderer_) {
    return false;
  }
  VkExtent2D extent{};
  if (!preview_renderer_->ReadbackTexture(out_rgba, &extent)) {
    return false;
  }
  if (out_size) {
    *out_size = ImVec2(static_cast<float>(extent.width), static_cast<float>(extent.height));
  }
  return true;
}

std::string ImGuiVulkanRuntime::RenderPreviewDebugSummary() const {
  if (!preview_renderer_) {
    return "preview_renderer=uninitialized";
  }
  if (pending_render_preview_request_.valid && result_image_.valid()) {
    return
      "request=valid stage=" + pending_render_preview_request_.stage_name +
      " buffers=" + std::to_string(pending_render_preview_request_.storage_buffers.size()) +
      " pipeline=ready target=ready extent=" +
      std::to_string(result_image_.extent.width) + "x" +
      std::to_string(result_image_.extent.height) +
      " vs=" + pending_render_preview_request_.vertex_shader_path +
      " fs=" + pending_render_preview_request_.fragment_shader_path;
  }
  return preview_renderer_->DebugSummary();
}

ImTextureID ImGuiVulkanRuntime::RenderPreviewTextureId() const {
  if (result_texture_descriptor_set_ != VK_NULL_HANDLE) {
    return (ImTextureID)(intptr_t)result_texture_descriptor_set_;
  }
  return preview_renderer_
    ? (ImTextureID)(intptr_t)preview_renderer_->PreviewTextureDescriptorSet()
    : ImTextureID{};
}

ImVec2 ImGuiVulkanRuntime::RenderPreviewTextureSize() const {
  if (result_image_.valid()) {
    return ImVec2(
      static_cast<float>(result_image_.extent.width),
      static_cast<float>(result_image_.extent.height));
  }
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
  const void* previous_execution_key = pending_render_preview_request_.execution_key;
  pending_render_preview_request_ = request;
  has_pending_render_preview_request_ = request.valid;
  if (!request.valid || request.execution_key != previous_execution_key) {
    RemoveResultTexture();
  }

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
  RefreshResultTexture();
}

void ImGuiVulkanRuntime::RemoveResultTexture() {
  if (result_texture_descriptor_set_ != VK_NULL_HANDLE) {
    ImGui_ImplVulkan_RemoveTexture(result_texture_descriptor_set_);
    result_texture_descriptor_set_ = VK_NULL_HANDLE;
  }
  result_image_ = {};
}

void ImGuiVulkanRuntime::RefreshResultTexture() {
  if (device_ == VK_NULL_HANDLE || !pending_render_preview_request_.valid ||
      pending_render_preview_request_.execution_key == nullptr) {
    return;
  }

  const RuntimeVkResultImage image =
    RuntimeVkContextRegistry::Instance().SnapshotResultImage(
      pending_render_preview_request_.execution_key);
  if (!image.valid()) {
    return;
  }
  if (image.view == result_image_.view &&
      image.sampler == result_image_.sampler &&
      image.layout == result_image_.layout) {
    result_image_ = image;
    return;
  }
  RemoveResultTexture();
  result_texture_descriptor_set_ = ImGui_ImplVulkan_AddTexture(
    image.sampler,
    image.view,
    image.layout);
  if (result_texture_descriptor_set_ == VK_NULL_HANDLE) {
    throw std::runtime_error("ImGui result texture descriptor allocation failed");
  }
  result_image_ = image;
}

void ImGuiVulkanRuntime::SetRenderPreviewExtent(ImVec2 extent) {
  if (preview_renderer_) {
    preview_renderer_->SetTargetExtent(VkExtent2D{
      static_cast<uint32_t>(extent.x),
      static_cast<uint32_t>(extent.y),
    });
  }
}

void ImGuiVulkanRuntime::ClearVkRuntimeCaches() {
  if (device_ != VK_NULL_HANDLE) {
    CheckVkResult(vkDeviceWaitIdle(device_));
  }
  RemoveResultTexture();
  RuntimeVkContextRegistry::Instance().ClearTransientState();
  InvokeRuntimeVkCacheClearCallback();
}

bool ImGuiVulkanRuntime::Tick(SDL_Window* window) {
  if (!initialized_ || !window) {
    return false;
  }

  if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
    SDL_Delay(10);
    return true;
  }

  ++preview_frame_sequence_;

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

  RefreshResultTexture();

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

  last_preview_frame_end_ = std::chrono::steady_clock::now();
  return true;
}

void ImGuiVulkanRuntime::Destroy() {
  if (!initialized_ && instance_ == VK_NULL_HANDLE && device_ == VK_NULL_HANDLE) {
    return;
  }

  try {
    ClearVkRuntimeCaches();
  } catch (...) {
    // Shutdown should keep going even if the device was already lost.
  }

  if (preview_renderer_) {
    preview_renderer_->Destroy();
    preview_renderer_.reset();
  }
  RemoveResultTexture();

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
