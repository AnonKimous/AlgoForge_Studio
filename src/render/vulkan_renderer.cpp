#include "renderer.h"

#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <imgui_impl_win32.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {

constexpr uint64_t kFrameTimeoutNs = 5ull * 1000ull * 1000ull * 1000ull;

std::vector<char> ReadBinaryFile(const char* path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error(std::string("Failed to open shader: ") + path);
  return std::vector<char>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

VkSurfaceKHR CreateWin32Surface(VkInstance instance, HINSTANCE hinstance, HWND hwnd) {
  VkWin32SurfaceCreateInfoKHR info{};
  info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  info.hinstance = hinstance;
  info.hwnd = hwnd;

  VkSurfaceKHR surface{};
  if (vkCreateWin32SurfaceKHR(instance, &info, nullptr, &surface) != VK_SUCCESS) {
    throw std::runtime_error("vkCreateWin32SurfaceKHR failed");
  }
  return surface;
}

VkExtent2D ClampExtent(const VkSurfaceCapabilitiesKHR& caps, uint32_t width, uint32_t height) {
  if (caps.currentExtent.width != UINT32_MAX) return caps.currentExtent;
  return VkExtent2D{
    std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width),
    std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height),
  };
}

void AppendLine(std::vector<GpuVertex>& vertices, float x0, float y0, float x1, float y1) {
  vertices.push_back({x0, y0, 0.0f, 0.0f, 1.0f, 0.25f});
  vertices.push_back({x1, y1, 0.0f, 0.0f, 1.0f, 0.25f});
}

const char* GlyphRows(char c) {
  switch (c) {
    case 'a': return "01110""10001""11111""10001""10001""10001""10001";
    case 'c': return "01111""10000""10000""10000""10000""10000""01111";
    case 'd': return "11110""10001""10001""10001""10001""10001""11110";
    case 'l': return "10000""10000""10000""10000""10000""10000""11111";
    case 'r': return "11110""10001""10001""11110""10100""10010""10001";
    case 'w': return "10001""10001""10001""10101""10101""10101""01010";
    case ':': return "00000""00100""00100""00000""00100""00100""00000";
    case '0': return "01110""10001""10011""10101""11001""10001""01110";
    case '1': return "00100""01100""00100""00100""00100""00100""01110";
    case '2': return "01110""10001""00001""00010""00100""01000""11111";
    case '3': return "11110""00001""00001""01110""00001""00001""11110";
    case '4': return "10010""10010""10010""11111""00010""00010""00010";
    case '5': return "11111""10000""10000""11110""00001""00001""11110";
    case '6': return "01110""10000""10000""11110""10001""10001""01110";
    case '7': return "11111""00001""00010""00100""01000""01000""01000";
    case '8': return "01110""10001""10001""01110""10001""10001""01110";
    case '9': return "01110""10001""10001""01111""00001""00001""01110";
    default: return "00000""00000""00000""00000""00000""00000""00000";
  }
}

void AppendText(std::vector<GpuVertex>& vertices, const std::string& text, float x, float y, float pixel) {
  for (char c : text) {
    if (c == ' ') {
      x += pixel * 3.0f;
      continue;
    }
    const char* rows = GlyphRows(c);
    for (int row = 0; row < 7; ++row) {
      for (int col = 0; col < 5; ++col) {
        if (rows[row * 5 + col] != '1') continue;
        float x0 = x + col * pixel;
        float y0 = y - row * pixel;
        float x1 = x0 + pixel * 0.65f;
        float y1 = y0;
        AppendLine(vertices, x0, y0, x1, y1);
      }
    }
    x += pixel * 6.0f;
  }
}

bool SelectionContainsEdge(const Mesh& mesh, const SelectionState& selection, const std::array<uint32_t, 2>& edge) {
  if (selection.kind != SelectionKind::Triangle || selection.triangle < 0) return false;
  const auto& tri = mesh.triangles[selection.triangle];
  auto same = [](const std::array<uint32_t, 2>& e, uint32_t a, uint32_t b) {
    return (e[0] == a && e[1] == b) || (e[0] == b && e[1] == a);
  };
  return same(edge, tri[0], tri[1]) || same(edge, tri[1], tri[2]) || same(edge, tri[2], tri[0]);
}

bool SelectionContainsVertex(const Mesh& mesh, const SelectionState& selection, uint32_t vertex) {
  if (selection.kind == SelectionKind::Vertex) return selection.vertex == static_cast<int>(vertex);
  if (selection.kind != SelectionKind::Triangle || selection.triangle < 0) return false;
  const auto& tri = mesh.triangles[selection.triangle];
  return tri[0] == vertex || tri[1] == vertex || tri[2] == vertex;
}

void AppendColoredLine(std::vector<GpuVertex>& vertices, Vec2 a, Vec2 b, float r, float g, float bl) {
  vertices.push_back({a.x, a.y, 0.0f, r, g, bl});
  vertices.push_back({b.x, b.y, 0.0f, r, g, bl});
}

void AppendHatchForTriangle(std::vector<GpuVertex>& vertices, const Mesh& mesh, uint32_t tri_index, float r, float g, float b) {
  const auto& tri = mesh.triangles[tri_index];
  Vec2 a{mesh.positions[tri[0]].x, mesh.positions[tri[0]].y};
  Vec2 bb{mesh.positions[tri[1]].x, mesh.positions[tri[1]].y};
  Vec2 c{mesh.positions[tri[2]].x, mesh.positions[tri[2]].y};

  Vec2 minp{std::min({a.x, bb.x, c.x}), std::min({a.y, bb.y, c.y})};
  Vec2 maxp{std::max({a.x, bb.x, c.x}), std::max({a.y, bb.y, c.y})};
  float span = (maxp.x - minp.x) + (maxp.y - minp.y);
  if (span <= 0.0f) return;

  auto inside = [&](Vec2 p) {
    float c0 = (bb.x - a.x) * (p.y - a.y) - (bb.y - a.y) * (p.x - a.x);
    float c1 = (c.x - bb.x) * (p.y - bb.y) - (c.y - bb.y) * (p.x - bb.x);
    float c2 = (a.x - c.x) * (p.y - c.y) - (a.y - c.y) * (p.x - c.x);
    bool has_negative = c0 < 0.0f || c1 < 0.0f || c2 < 0.0f;
    bool has_positive = c0 > 0.0f || c1 > 0.0f || c2 > 0.0f;
    return !(has_negative && has_positive);
  };

  constexpr int kSamples = 36;
  float step = span / 9.0f;
  for (float k = minp.y - maxp.x - step * 2.0f; k <= maxp.y - minp.x + step * 2.0f; k += step) {
    bool active = false;
    Vec2 start{};
    Vec2 prev{};
    for (int i = 0; i <= kSamples; ++i) {
      float x = minp.x + (maxp.x - minp.x) * static_cast<float>(i) / static_cast<float>(kSamples);
      Vec2 p{x, x + k};
      bool hit = p.y >= minp.y && p.y <= maxp.y && inside(p);
      if (hit && !active) {
        start = p;
        active = true;
      }
      if (!hit && active) {
        AppendColoredLine(vertices, start, prev, r, g, b);
        active = false;
      }
      prev = p;
    }
    if (active) {
      AppendColoredLine(vertices, start, prev, r, g, b);
    }
  }
}

void AppendDashedArrow(std::vector<GpuVertex>& vertices, Vec3 a, Vec3 b, float r, float g, float bl, float phase) {
  Vec2 start{a.x, a.y};
  Vec2 end{b.x, b.y};
  Vec2 delta{end.x - start.x, end.y - start.y};
  float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
  if (length <= 1e-5f) return;

  Vec2 dir{delta.x / length, delta.y / length};
  Vec2 normal{-dir.y, dir.x};
  constexpr float dash = 0.055f;
  constexpr float gap = 0.04f;
  constexpr float period = dash + gap;
  float offset = std::fmod(phase * 0.22f, period);
  if (offset < 0.0f) offset += period;

  for (float t = -offset; t < length; t += period) {
    float t0 = std::max(0.0f, t);
    float t1 = std::min(length, t + dash);
    if (t1 <= 0.0f || t0 >= length || t1 <= t0) continue;
    Vec2 p0{start.x + dir.x * t0, start.y + dir.y * t0};
    Vec2 p1{start.x + dir.x * t1, start.y + dir.y * t1};
    AppendColoredLine(vertices, p0, p1, r, g, bl);
  }

  float head_len = std::min(0.055f, length * 0.28f);
  float head_width = head_len * 0.65f;
  Vec2 base{end.x - dir.x * head_len, end.y - dir.y * head_len};
  Vec2 left{base.x + normal.x * head_width, base.y + normal.y * head_width};
  Vec2 right{base.x - normal.x * head_width, base.y - normal.y * head_width};
  AppendColoredLine(vertices, end, left, r, g, bl);
  AppendColoredLine(vertices, end, right, r, g, bl);
}

}  // namespace

VulkanRenderer::VulkanRenderer(const Win32Window& window) {
  CreateInstanceAndDevice(window);
  CreateSwapchain();
  CreatePipeline();
  CreateFrameResources();
  CreateImGui(window);
  CreateVertexBuffer(256 * sizeof(GpuVertex));
}

VulkanRenderer::~VulkanRenderer() {
  if (device_) vkDeviceWaitIdle(device_);
  DestroyImGui();
  DestroyBuffer(vertex_buffer_);
  DestroyFrameResources();
  DestroyPipeline();
  DestroySwapchain();
  if (device_) vkDestroyDevice(device_, nullptr);
  if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
  if (instance_) vkDestroyInstance(instance_, nullptr);
}

void VulkanRenderer::CreateInstanceAndDevice(const Win32Window& window) {
  auto instance_ret = vkb::InstanceBuilder{}
    .set_app_name("Vulkan Mesh Editor")
    .request_validation_layers()
    .use_default_debug_messenger()
    .require_api_version(1, 3, 0)
    .enable_extension(VK_KHR_SURFACE_EXTENSION_NAME)
    .enable_extension(VK_KHR_WIN32_SURFACE_EXTENSION_NAME)
    .build();
  if (!instance_ret) throw std::runtime_error(instance_ret.error().message());
  vkb::Instance vkb_instance = instance_ret.value();
  instance_ = vkb_instance.instance;
  surface_ = CreateWin32Surface(instance_, window.hinstance(), window.hwnd());

  auto physical_ret = vkb::PhysicalDeviceSelector{vkb_instance}.set_surface(surface_).select();
  if (!physical_ret) throw std::runtime_error(physical_ret.error().message());
  vkb::PhysicalDevice selected = physical_ret.value();
  physical_device_ = selected.physical_device;

  auto graphics_q = selected.get_queue_index(VK_QUEUE_GRAPHICS_BIT);
  auto present_q = selected.get_queue_index(surface_);
  if (!graphics_q || !present_q) throw std::runtime_error("No required queue family found");
  graphics_queue_family_ = graphics_q.value();
  present_queue_family_ = present_q.value();

  float priority = 1.0f;
  std::vector<VkDeviceQueueCreateInfo> queues;
  std::vector<uint32_t> families = {graphics_queue_family_};
  if (present_queue_family_ != graphics_queue_family_) families.push_back(present_queue_family_);
  for (uint32_t family : families) {
    VkDeviceQueueCreateInfo queue{};
    queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue.queueFamilyIndex = family;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;
    queues.push_back(queue);
  }

  const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  VkPhysicalDeviceFeatures features{};
  VkPhysicalDeviceVulkan13Features features13{};
  features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
  features13.dynamicRendering = VK_TRUE;
  VkDeviceCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  info.pNext = &features13;
  info.queueCreateInfoCount = static_cast<uint32_t>(queues.size());
  info.pQueueCreateInfos = queues.data();
  info.pEnabledFeatures = &features;
  info.enabledExtensionCount = 1;
  info.ppEnabledExtensionNames = extensions;

  if (vkCreateDevice(physical_device_, &info, nullptr, &device_) != VK_SUCCESS) {
    throw std::runtime_error("vkCreateDevice failed");
  }
  vkGetDeviceQueue(device_, graphics_queue_family_, 0, &graphics_queue_);
  vkGetDeviceQueue(device_, present_queue_family_, 0, &present_queue_);
}

void VulkanRenderer::CreateSwapchain() {
  VkSurfaceCapabilitiesKHR caps{};
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &caps);

  uint32_t format_count = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count, nullptr);
  std::vector<VkSurfaceFormatKHR> formats(format_count);
  vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count, formats.data());

  VkSurfaceFormatKHR chosen_format = formats[0];
  for (const auto& format : formats) {
    if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      chosen_format = format;
      break;
    }
  }

  uint32_t image_count = caps.minImageCount + 1;
  if (caps.maxImageCount > 0) image_count = std::min(image_count, caps.maxImageCount);

  uint32_t indices[] = {graphics_queue_family_, present_queue_family_};
  VkSwapchainCreateInfoKHR info{};
  info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  info.surface = surface_;
  info.minImageCount = image_count;
  info.imageFormat = chosen_format.format;
  info.imageColorSpace = chosen_format.colorSpace;
  info.imageExtent = ClampExtent(caps, 1280, 720);
  info.imageArrayLayers = 1;
  info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  info.preTransform = caps.currentTransform;
  info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  info.clipped = VK_TRUE;
  if (graphics_queue_family_ != present_queue_family_) {
    info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    info.queueFamilyIndexCount = 2;
    info.pQueueFamilyIndices = indices;
  } else {
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  }

  if (vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_) != VK_SUCCESS) {
    throw std::runtime_error("vkCreateSwapchainKHR failed");
  }

  swapchain_format_ = chosen_format.format;
  swapchain_extent_ = info.imageExtent;
  uint32_t actual_count = 0;
  vkGetSwapchainImagesKHR(device_, swapchain_, &actual_count, nullptr);
  swapchain_images_.resize(actual_count);
  vkGetSwapchainImagesKHR(device_, swapchain_, &actual_count, swapchain_images_.data());

  for (VkImage image : swapchain_images_) {
    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = swapchain_format_;
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view.subresourceRange.levelCount = 1;
    view.subresourceRange.layerCount = 1;
    VkImageView image_view{};
    if (vkCreateImageView(device_, &view, nullptr, &image_view) != VK_SUCCESS) {
      throw std::runtime_error("vkCreateImageView failed");
    }
    swapchain_image_views_.push_back(image_view);
  }
}

VkShaderModule VulkanRenderer::CreateShaderModule(const char* path) const {
  auto code = ReadBinaryFile(path);
  VkShaderModuleCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  info.codeSize = code.size();
  info.pCode = reinterpret_cast<const uint32_t*>(code.data());
  VkShaderModule module{};
  if (vkCreateShaderModule(device_, &info, nullptr, &module) != VK_SUCCESS) {
    throw std::runtime_error("vkCreateShaderModule failed");
  }
  return module;
}

void VulkanRenderer::CreatePipeline() {
  VkPipelineLayoutCreateInfo layout{};
  layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  if (vkCreatePipelineLayout(device_, &layout, nullptr, &pipeline_layout_) != VK_SUCCESS) {
    throw std::runtime_error("vkCreatePipelineLayout failed");
  }

  line_pipeline_ = CreateGraphicsPipeline(VK_PRIMITIVE_TOPOLOGY_LINE_LIST, 1.0f);
  point_pipeline_ = CreateGraphicsPipeline(VK_PRIMITIVE_TOPOLOGY_POINT_LIST, 14.0f);
}

VkPipeline VulkanRenderer::CreateGraphicsPipeline(VkPrimitiveTopology topology, float point_size) {
  VkShaderModule vert = CreateShaderModule(SHADER_VERT_PATH);
  VkShaderModule frag = CreateShaderModule(SHADER_FRAG_PATH);

  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vert;
  stages[0].pName = "main";
  VkSpecializationMapEntry point_size_entry{0, 0, sizeof(float)};
  VkSpecializationInfo spec{};
  spec.mapEntryCount = 1;
  spec.pMapEntries = &point_size_entry;
  spec.dataSize = sizeof(float);
  spec.pData = &point_size;
  stages[0].pSpecializationInfo = &spec;
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = frag;
  stages[1].pName = "main";

  VkVertexInputBindingDescription binding{0, sizeof(GpuVertex), VK_VERTEX_INPUT_RATE_VERTEX};
  std::array<VkVertexInputAttributeDescription, 2> attrs{};
  attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GpuVertex, px)};
  attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GpuVertex, r)};
  VkPipelineVertexInputStateCreateInfo vertex_input{};
  vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertex_input.vertexBindingDescriptionCount = 1;
  vertex_input.pVertexBindingDescriptions = &binding;
  vertex_input.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
  vertex_input.pVertexAttributeDescriptions = attrs.data();

  VkPipelineInputAssemblyStateCreateInfo input{};
  input.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  input.topology = topology;

  VkViewport viewport{};
  viewport.width = static_cast<float>(swapchain_extent_.width);
  viewport.height = static_cast<float>(swapchain_extent_.height);
  viewport.maxDepth = 1.0f;
  VkRect2D scissor{};
  scissor.extent = swapchain_extent_;
  VkPipelineViewportStateCreateInfo viewport_state{};
  viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport_state.viewportCount = 1;
  viewport_state.pViewports = &viewport;
  viewport_state.scissorCount = 1;
  viewport_state.pScissors = &scissor;

  VkPipelineRasterizationStateCreateInfo raster{};
  raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.lineWidth = 1.0f;
  raster.cullMode = VK_CULL_MODE_NONE;
  raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

  VkPipelineMultisampleStateCreateInfo ms{};
  ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState blend_att{};
  blend_att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo blend{};
  blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  blend.attachmentCount = 1;
  blend.pAttachments = &blend_att;

  VkPipelineRenderingCreateInfo rendering{};
  rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachmentFormats = &swapchain_format_;

  VkGraphicsPipelineCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  info.pNext = &rendering;
  info.stageCount = 2;
  info.pStages = stages;
  info.pVertexInputState = &vertex_input;
  info.pInputAssemblyState = &input;
  info.pViewportState = &viewport_state;
  info.pRasterizationState = &raster;
  info.pMultisampleState = &ms;
  info.pColorBlendState = &blend;
  info.layout = pipeline_layout_;
  info.renderPass = VK_NULL_HANDLE;

  VkPipeline pipeline{};
  VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
  vkDestroyShaderModule(device_, frag, nullptr);
  vkDestroyShaderModule(device_, vert, nullptr);
  if (result != VK_SUCCESS) throw std::runtime_error("vkCreateGraphicsPipelines failed");
  return pipeline;
}

void VulkanRenderer::CreateFrameResources() {
  VkCommandPoolCreateInfo pool{};
  pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pool.queueFamilyIndex = graphics_queue_family_;
  if (vkCreateCommandPool(device_, &pool, nullptr, &command_pool_) != VK_SUCCESS) {
    throw std::runtime_error("vkCreateCommandPool failed");
  }

  command_buffers_.resize(swapchain_images_.size());
  VkCommandBufferAllocateInfo alloc{};
  alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  alloc.commandPool = command_pool_;
  alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  alloc.commandBufferCount = static_cast<uint32_t>(command_buffers_.size());
  if (vkAllocateCommandBuffers(device_, &alloc, command_buffers_.data()) != VK_SUCCESS) {
    throw std::runtime_error("vkAllocateCommandBuffers failed");
  }

  VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  if (vkCreateSemaphore(device_, &sem, nullptr, &image_available_) != VK_SUCCESS ||
      vkCreateSemaphore(device_, &sem, nullptr, &render_finished_) != VK_SUCCESS) {
    throw std::runtime_error("vkCreateSemaphore failed");
  }
  VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  if (vkCreateFence(device_, &fence, nullptr, &in_flight_) != VK_SUCCESS) {
    throw std::runtime_error("vkCreateFence failed");
  }
}

void VulkanRenderer::CreateImGui(const Win32Window& window) {
  VkDescriptorPoolSize pool_sizes[] = {
    {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
    {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
    {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
    {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
    {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000},
  };

  VkDescriptorPoolCreateInfo pool_info{};
  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  pool_info.maxSets = 1000 * static_cast<uint32_t>(std::size(pool_sizes));
  pool_info.poolSizeCount = static_cast<uint32_t>(std::size(pool_sizes));
  pool_info.pPoolSizes = pool_sizes;
  if (vkCreateDescriptorPool(device_, &pool_info, nullptr, &imgui_descriptor_pool_) != VK_SUCCESS) {
    throw std::runtime_error("vkCreateDescriptorPool for ImGui failed");
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();

  ImGui_ImplWin32_Init(window.hwnd());

  VkPipelineRenderingCreateInfoKHR pipeline_rendering{};
  pipeline_rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
  pipeline_rendering.colorAttachmentCount = 1;
  pipeline_rendering.pColorAttachmentFormats = &swapchain_format_;

  ImGui_ImplVulkan_InitInfo init_info{};
  init_info.ApiVersion = VK_API_VERSION_1_3;
  init_info.Instance = instance_;
  init_info.PhysicalDevice = physical_device_;
  init_info.Device = device_;
  init_info.QueueFamily = graphics_queue_family_;
  init_info.Queue = graphics_queue_;
  init_info.DescriptorPool = imgui_descriptor_pool_;
  init_info.MinImageCount = static_cast<uint32_t>(swapchain_images_.size());
  init_info.ImageCount = static_cast<uint32_t>(swapchain_images_.size());
  init_info.UseDynamicRendering = true;
  init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  init_info.PipelineInfoMain.PipelineRenderingCreateInfo = pipeline_rendering;
  ImGui_ImplVulkan_Init(&init_info);
}

void VulkanRenderer::CreateVertexBuffer(size_t size) {
  vertex_buffer_.size = size;
  VkBufferCreateInfo buffer{};
  buffer.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer.size = size;
  buffer.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  buffer.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(device_, &buffer, nullptr, &vertex_buffer_.buffer) != VK_SUCCESS) {
    throw std::runtime_error("vkCreateBuffer failed");
  }

  VkMemoryRequirements req{};
  vkGetBufferMemoryRequirements(device_, vertex_buffer_.buffer, &req);
  VkMemoryAllocateInfo alloc{};
  alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  alloc.allocationSize = req.size;
  alloc.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (vkAllocateMemory(device_, &alloc, nullptr, &vertex_buffer_.memory) != VK_SUCCESS) {
    throw std::runtime_error("vkAllocateMemory failed");
  }
  vkBindBufferMemory(device_, vertex_buffer_.buffer, vertex_buffer_.memory, 0);
}

void VulkanRenderer::EnsureVertexBufferSize(size_t size) {
  if (size <= vertex_buffer_.size) return;
  vkDeviceWaitIdle(device_);
  DestroyBuffer(vertex_buffer_);
  CreateVertexBuffer(size * 2);
}

void VulkanRenderer::AppendHudVertices(std::vector<GpuVertex>& vertices, const FrameStats& stats) {
  float x = -0.96f;
  float y = 0.92f;
  float pixel = 0.012f;
  AppendText(vertices, "drawcall:" + std::to_string(stats.draw_calls), x, y, pixel);
}

void VulkanRenderer::UpdateVertexBuffer(const Mesh& mesh, const TriangleOrientationAnalyzer& analyzer, int highlighted_vertex, const RenderUiState& ui_state, uint32_t& green_hatch_count, uint32_t& yellow_hatch_count, uint32_t& red_hatch_count, uint32_t& line_count, uint32_t& directive_line_count, uint32_t& point_count) {
  std::vector<GpuVertex> vertices;
  vertices.reserve(mesh.edges.size() * 2 + mesh.positions.size() + mesh.triangles.size() * 80 + ui_state.phys_directives.size() * 48);
  const auto& negative_edges = analyzer.edge_has_negative_triangle();
  const auto& negative_vertices = analyzer.vertex_has_negative_triangle();
  const auto& triangle_faces_viewer = analyzer.triangle_faces_viewer();
  const SelectionState& selection = ui_state.selection;

  for (uint32_t tri_index = 0; tri_index < mesh.triangles.size(); ++tri_index) {
    if (selection.kind == SelectionKind::Triangle && selection.triangle == static_cast<int>(tri_index)) continue;
    bool negative = tri_index < triangle_faces_viewer.size() && !triangle_faces_viewer[tri_index];
    if (!negative) AppendHatchForTriangle(vertices, mesh, tri_index, 0.0f, 0.75f, 0.25f);
  }
  green_hatch_count = static_cast<uint32_t>(vertices.size());

  for (uint32_t tri_index = 0; tri_index < mesh.triangles.size(); ++tri_index) {
    if (selection.kind == SelectionKind::Triangle && selection.triangle == static_cast<int>(tri_index)) continue;
    bool negative = tri_index < triangle_faces_viewer.size() && !triangle_faces_viewer[tri_index];
    if (negative) AppendHatchForTriangle(vertices, mesh, tri_index, 1.0f, 0.85f, 0.0f);
  }
  yellow_hatch_count = static_cast<uint32_t>(vertices.size()) - green_hatch_count;

  if (selection.kind == SelectionKind::Triangle && selection.triangle >= 0) {
    AppendHatchForTriangle(vertices, mesh, static_cast<uint32_t>(selection.triangle), 1.0f, 0.0f, 0.0f);
  }
  red_hatch_count = static_cast<uint32_t>(vertices.size()) - green_hatch_count - yellow_hatch_count;

  uint32_t line_start = static_cast<uint32_t>(vertices.size());
  for (uint32_t edge_index = 0; edge_index < mesh.edges.size(); ++edge_index) {
    const auto& edge = mesh.edges[edge_index];
    bool negative = edge_index < negative_edges.size() && negative_edges[edge_index];
    bool selected = SelectionContainsEdge(mesh, selection, edge);
    float r = selected ? 1.0f : (negative ? 1.0f : 0.0f);
    float g = selected ? 1.0f : (negative ? 0.85f : 0.9f);
    float b = selected ? 1.0f : (negative ? 0.0f : 0.25f);
    for (uint32_t index : edge) {
      const Vec3& p = mesh.positions[index];
      vertices.push_back({p.x, p.y, p.z, r, g, b});
    }
  }
  line_count = static_cast<uint32_t>(vertices.size()) - line_start;

  uint32_t directive_start = static_cast<uint32_t>(vertices.size());
  if (ui_state.mode == InteractionMode::Phys && ui_state.phys_sub_mode == PhysSubMode::Guide) {
    for (const PhysDirective& directive : ui_state.phys_directives) {
      if (directive.vertex < 0) continue;
      if (!directive.valid) {
        AppendDashedArrow(vertices, directive.start, directive.requested_target, 1.0f, 0.0f, 0.0f, ui_state.animation_time);
      }
      AppendDashedArrow(vertices, directive.start, directive.allowed_target, 1.0f, 1.0f, 1.0f, ui_state.animation_time);
    }
  }
  directive_line_count = static_cast<uint32_t>(vertices.size()) - directive_start;

  for (uint32_t i = 0; i < mesh.positions.size(); ++i) {
    const Vec3& p = mesh.positions[i];
    bool hot = static_cast<int>(i) == highlighted_vertex;
    bool negative = i < negative_vertices.size() && negative_vertices[i];
    bool selected = SelectionContainsVertex(mesh, selection, i);
    float r = selected ? 1.0f : (negative ? 1.0f : 0.0f);
    float g = selected ? 1.0f : (negative ? 0.85f : 0.9f);
    float b = selected ? 1.0f : (negative ? 0.0f : 0.25f);
    if (hot) {
      r = 1.0f;
      g = 0.1f;
      b = 0.05f;
    }
    vertices.push_back({p.x, p.y, p.z, r, g, b});
  }
  point_count = static_cast<uint32_t>(mesh.positions.size());

  size_t bytes = vertices.size() * sizeof(GpuVertex);
  EnsureVertexBufferSize(bytes);

  void* mapped = nullptr;
  vkMapMemory(device_, vertex_buffer_.memory, 0, bytes, 0, &mapped);
  std::memcpy(mapped, vertices.data(), bytes);
  vkUnmapMemory(device_, vertex_buffer_.memory);
}

RenderFrameResult VulkanRenderer::Draw(const Mesh& mesh, const TriangleOrientationAnalyzer& analyzer, int highlighted_vertex, const RenderUiState& ui_state) {
  uint32_t green_hatch_count = 0;
  uint32_t yellow_hatch_count = 0;
  uint32_t red_hatch_count = 0;
  uint32_t line_count = 0;
  uint32_t directive_line_count = 0;
  uint32_t point_count = 0;
  UpdateVertexBuffer(mesh, analyzer, highlighted_vertex, ui_state, green_hatch_count, yellow_hatch_count, red_hatch_count, line_count, directive_line_count, point_count);
  FrameStats stats{};
  stats.draw_calls = 6;
  bool save_requested = false;

  VkResult fence_wait = vkWaitForFences(device_, 1, &in_flight_, VK_TRUE, kFrameTimeoutNs);
  if (fence_wait == VK_TIMEOUT) throw std::runtime_error("Timed out waiting for GPU fence");
  if (fence_wait != VK_SUCCESS) throw std::runtime_error("vkWaitForFences failed");
  vkResetFences(device_, 1, &in_flight_);

  uint32_t image_index = 0;
  VkResult acquire = vkAcquireNextImageKHR(device_, swapchain_, kFrameTimeoutNs, image_available_, VK_NULL_HANDLE, &image_index);
  if (acquire == VK_TIMEOUT) throw std::runtime_error("Timed out waiting for swapchain image");
  if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) throw std::runtime_error("vkAcquireNextImageKHR failed");

  VkCommandBuffer cmd = command_buffers_[image_index];
  vkResetCommandBuffer(cmd, 0);
  VkCommandBufferBeginInfo begin{};
  begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  vkBeginCommandBuffer(cmd, &begin);

  TransitionSwapchainImage(cmd, swapchain_images_[image_index], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

  VkClearValue clear{};
  clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  VkRenderingAttachmentInfo color_attachment{};
  color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  color_attachment.imageView = swapchain_image_views_[image_index];
  color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color_attachment.clearValue = clear;

  VkRenderingInfo rendering{};
  rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  rendering.renderArea.extent = swapchain_extent_;
  rendering.layerCount = 1;
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachments = &color_attachment;
  vkCmdBeginRendering(cmd, &rendering);

  VkDeviceSize offset = 0;
  vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer_.buffer, &offset);
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, line_pipeline_);
  uint32_t first = 0;
  vkCmdDraw(cmd, green_hatch_count, 1, first, 0);
  first += green_hatch_count;
  vkCmdDraw(cmd, yellow_hatch_count, 1, first, 0);
  first += yellow_hatch_count;
  vkCmdDraw(cmd, red_hatch_count, 1, first, 0);
  first += red_hatch_count;
  vkCmdDraw(cmd, line_count, 1, first, 0);
  first += line_count;
  vkCmdDraw(cmd, directive_line_count, 1, first, 0);
  first += directive_line_count;
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, point_pipeline_);
  vkCmdDraw(cmd, point_count, 1, first, 0);
  stats.line_vertices = line_count;
  stats.point_vertices = point_count;

  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();
  ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.65f);
  ImGui::Begin("Stats", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);
  ImGui::Text("drawcall: %u", stats.draw_calls);
  if (ImGui::Button(mode_ == InteractionMode::Edit ? "mode: edit" : "mode: phys")) {
    mode_ = mode_ == InteractionMode::Edit ? InteractionMode::Phys : InteractionMode::Edit;
  }
  if (mode_ == InteractionMode::Phys) {
    if (ImGui::Button(phys_sub_mode_ == PhysSubMode::Guide ? "phys: guide" : "phys: run")) {
      phys_sub_mode_ = phys_sub_mode_ == PhysSubMode::Guide ? PhysSubMode::Run : PhysSubMode::Guide;
    }
  }
  ImGui::End();

  if (mode_ == InteractionMode::Edit) {
    ImGui::SetNextWindowPos(ImVec2(10.0f, 90.0f), ImGuiCond_Once);
    ImGui::Begin("Edit Mode");
    ImGui::Text("mesh: %s", ui_state.mesh_file_name.c_str());
    if (ui_state.selection.kind == SelectionKind::Vertex && ui_state.selection.vertex >= 0) {
      ImGui::Text("vertex %d", ui_state.selection.vertex);
      ImGui::Text("x: %.4f", ui_state.selected_vertex_position.x);
      ImGui::Text("y: %.4f", ui_state.selected_vertex_position.y);
      ImGui::Text("z: %.4f", ui_state.selected_vertex_position.z);
    } else if (ui_state.selection.kind == SelectionKind::Triangle && ui_state.selection.triangle >= 0) {
      ImGui::Text("triangle %d", ui_state.selection.triangle);
    }
    if (ImGui::Button("Save")) {
      save_requested = true;
    }
    ImGui::End();
  }

  ImGui::Render();
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

  vkCmdEndRendering(cmd);
  TransitionSwapchainImage(cmd, swapchain_images_[image_index], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
  if (vkEndCommandBuffer(cmd) != VK_SUCCESS) throw std::runtime_error("vkEndCommandBuffer failed");

  VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo submit{};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.waitSemaphoreCount = 1;
  submit.pWaitSemaphores = &image_available_;
  submit.pWaitDstStageMask = &wait_stage;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &cmd;
  submit.signalSemaphoreCount = 1;
  submit.pSignalSemaphores = &render_finished_;
  if (vkQueueSubmit(graphics_queue_, 1, &submit, in_flight_) != VK_SUCCESS) {
    throw std::runtime_error("vkQueueSubmit failed");
  }

  VkPresentInfoKHR present{};
  present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  present.waitSemaphoreCount = 1;
  present.pWaitSemaphores = &render_finished_;
  present.swapchainCount = 1;
  present.pSwapchains = &swapchain_;
  present.pImageIndices = &image_index;
  VkResult present_result = vkQueuePresentKHR(present_queue_, &present);
  if (present_result != VK_SUCCESS && present_result != VK_SUBOPTIMAL_KHR) {
    throw std::runtime_error("vkQueuePresentKHR failed");
  }
  return RenderFrameResult{stats.draw_calls, mode_, phys_sub_mode_, save_requested};
}

uint32_t VulkanRenderer::FindMemoryType(uint32_t type_filter, VkMemoryPropertyFlags properties) const {
  VkPhysicalDeviceMemoryProperties mem{};
  vkGetPhysicalDeviceMemoryProperties(physical_device_, &mem);
  for (uint32_t i = 0; i < mem.memoryTypeCount; ++i) {
    if ((type_filter & (1u << i)) && (mem.memoryTypes[i].propertyFlags & properties) == properties) {
      return i;
    }
  }
  throw std::runtime_error("No suitable memory type found");
}

void VulkanRenderer::TransitionSwapchainImage(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout) {
  VkImageMemoryBarrier2 barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
  barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
  barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
  barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
  barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
  barrier.oldLayout = old_layout;
  barrier.newLayout = new_layout;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.layerCount = 1;

  if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    barrier.srcAccessMask = 0;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
  } else if (old_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
    barrier.dstAccessMask = 0;
  }

  VkDependencyInfo dependency{};
  dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  dependency.imageMemoryBarrierCount = 1;
  dependency.pImageMemoryBarriers = &barrier;
  vkCmdPipelineBarrier2(cmd, &dependency);
}

void VulkanRenderer::DestroyBuffer(Buffer& buffer) {
  if (buffer.buffer) vkDestroyBuffer(device_, buffer.buffer, nullptr);
  if (buffer.memory) vkFreeMemory(device_, buffer.memory, nullptr);
  buffer = {};
}

void VulkanRenderer::DestroyFrameResources() {
  if (in_flight_) vkDestroyFence(device_, in_flight_, nullptr);
  if (image_available_) vkDestroySemaphore(device_, image_available_, nullptr);
  if (render_finished_) vkDestroySemaphore(device_, render_finished_, nullptr);
  if (command_pool_) vkDestroyCommandPool(device_, command_pool_, nullptr);
}

void VulkanRenderer::DestroyImGui() {
  if (!imgui_descriptor_pool_) return;
  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();
  vkDestroyDescriptorPool(device_, imgui_descriptor_pool_, nullptr);
  imgui_descriptor_pool_ = VK_NULL_HANDLE;
}

void VulkanRenderer::DestroyPipeline() {
  if (point_pipeline_) vkDestroyPipeline(device_, point_pipeline_, nullptr);
  if (line_pipeline_) vkDestroyPipeline(device_, line_pipeline_, nullptr);
  if (pipeline_layout_) vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
}

void VulkanRenderer::DestroySwapchain() {
  for (VkImageView view : swapchain_image_views_) vkDestroyImageView(device_, view, nullptr);
  if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
}
