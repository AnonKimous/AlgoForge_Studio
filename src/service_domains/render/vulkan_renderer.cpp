#include "renderer.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <optional>
#include <stdexcept>

namespace service_domains {
#include <string>

namespace {

constexpr uint64_t kFrameTimeoutNs = 5ull * 1000ull * 1000ull * 1000ull;

std::vector<char> ReadBinaryFile(const char* path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error(std::string("Failed to open shader: ") + path);
  return std::vector<char>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

std::string ResolveImGuiIniPath() {
  const char* base_path = SDL_GetBasePath();
  if (!base_path || base_path[0] == '\0') {
    return "imgui.ini";
  }
  std::filesystem::path path(base_path);
  path = path.parent_path() / ".." / "imgui.ini";
  return path.lexically_normal().string();
}

VkSurfaceKHR CreateSdlSurface(VkInstance instance, SDL_Window* window) {
  VkSurfaceKHR surface{};
  if (SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface) == 0) {
    throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
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

void AppendColoredLine(std::vector<GpuVertex>& vertices, Vec2 a, Vec2 b, float r, float g, float bl) {
  vertices.push_back({a.x, a.y, 0.0f, r, g, bl});
  vertices.push_back({b.x, b.y, 0.0f, r, g, bl});
}

void AppendColoredLine(std::vector<GpuVertex>& vertices, Vec3 a, Vec3 b, float r, float g, float bl) {
  AppendColoredLine(vertices, Vec2{a.x, a.y}, Vec2{b.x, b.y}, r, g, bl);
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

void AppendDashedArrow(std::vector<GpuVertex>& vertices, Vec3 a, Vec3 b, float r, float g, float bl, float phase, float thickness = 0.0f) {
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

  auto draw_shifted = [&](float shift) {
    Vec2 shifted_start{start.x + normal.x * shift, start.y + normal.y * shift};
    Vec2 shifted_end{end.x + normal.x * shift, end.y + normal.y * shift};
    Vec2 shifted_delta{shifted_end.x - shifted_start.x, shifted_end.y - shifted_start.y};
    float shifted_length = std::sqrt(shifted_delta.x * shifted_delta.x + shifted_delta.y * shifted_delta.y);
    if (shifted_length <= 1e-5f) return;
    Vec2 shifted_dir{shifted_delta.x / shifted_length, shifted_delta.y / shifted_length};
    Vec2 shifted_normal{-shifted_dir.y, shifted_dir.x};

    for (float t = -offset; t < shifted_length; t += period) {
      float t0 = std::max(0.0f, t);
      float t1 = std::min(shifted_length, t + dash);
      if (t1 <= 0.0f || t0 >= shifted_length || t1 <= t0) continue;
      Vec2 p0{shifted_start.x + shifted_dir.x * t0, shifted_start.y + shifted_dir.y * t0};
      Vec2 p1{shifted_start.x + shifted_dir.x * t1, shifted_start.y + shifted_dir.y * t1};
      AppendColoredLine(vertices, p0, p1, r, g, bl);
    }

    float head_len = std::min(0.055f, shifted_length * 0.28f);
    float head_width = head_len * 0.65f;
    Vec2 base{shifted_end.x - shifted_dir.x * head_len, shifted_end.y - shifted_dir.y * head_len};
    Vec2 left{base.x + shifted_normal.x * head_width, base.y + shifted_normal.y * head_width};
    Vec2 right{base.x - shifted_normal.x * head_width, base.y - shifted_normal.y * head_width};
    AppendColoredLine(vertices, shifted_end, left, r, g, bl);
    AppendColoredLine(vertices, shifted_end, right, r, g, bl);
  };

  if (thickness <= 0.0f) {
    draw_shifted(0.0f);
    return;
  }

  draw_shifted(-thickness);
  draw_shifted(0.0f);
  draw_shifted(thickness);
}

struct QueueFamilies {
  std::optional<uint32_t> graphics;
  std::optional<uint32_t> present;
};

QueueFamilies FindQueueFamilies(VkPhysicalDevice physical_device, VkSurfaceKHR surface) {
  QueueFamilies queues{};

  uint32_t queue_family_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, nullptr);
  std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
  vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_families.data());

  for (uint32_t i = 0; i < queue_family_count; ++i) {
    if (!queues.graphics && (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
      queues.graphics = i;
    }

    if (!queues.present) {
      VkBool32 supported = VK_FALSE;
      vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, i, surface, &supported);
      if (supported == VK_TRUE) {
        queues.present = i;
      }
    }

    if (queues.graphics && queues.present) break;
  }

  return queues;
}

void AppendArrow(std::vector<GpuVertex>& vertices, Vec3 a, Vec3 b, float r, float g, float bl) {
  AppendColoredLine(vertices, a, b, r, g, bl);
  Vec2 delta{b.x - a.x, b.y - a.y};
  float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
  if (length <= 1e-5f) return;
  Vec2 dir{delta.x / length, delta.y / length};
  Vec2 normal{-dir.y, dir.x};
  float head = std::clamp(length * 0.18f, 0.018f, 0.055f);
  Vec3 head_left{b.x - dir.x * head + normal.x * head * 0.65f, b.y - dir.y * head + normal.y * head * 0.65f, b.z};
  Vec3 head_right{b.x - dir.x * head - normal.x * head * 0.65f, b.y - dir.y * head - normal.y * head * 0.65f, b.z};
  AppendColoredLine(vertices, b, head_left, r, g, bl);
  AppendColoredLine(vertices, b, head_right, r, g, bl);
}

void AppendRingWithTicks(std::vector<GpuVertex>& vertices, Vec3 center, float radius, float r, float g, float b, float phase) {
  constexpr int kSegments = 28;
  constexpr int kTickCount = 12;
  constexpr float kPi = 3.1415926535f;
  Vec3 prev{};
  for (int i = 0; i <= kSegments; ++i) {
    float angle = (static_cast<float>(i) / static_cast<float>(kSegments)) * 2.0f * kPi + phase * 0.25f;
    Vec3 point{center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius, center.z};
    if (i > 0) {
      AppendColoredLine(vertices, prev, point, r, g, b);
    }
    prev = point;
  }

  for (int i = 0; i < kTickCount; ++i) {
    float angle = (static_cast<float>(i) / static_cast<float>(kTickCount)) * 2.0f * kPi + phase * 0.08f;
    Vec2 dir{std::cos(angle), std::sin(angle)};
    Vec2 normal{-dir.y, dir.x};
    float tick_len = (i % 3 == 0) ? radius * 0.45f : radius * 0.28f;
    Vec3 inner{center.x + dir.x * (radius - tick_len), center.y + dir.y * (radius - tick_len), center.z};
    Vec3 outer{center.x + dir.x * radius, center.y + dir.y * radius, center.z};
    AppendColoredLine(vertices, inner, outer, r, g, b);
    if (i % 3 == 0) {
      Vec3 left{center.x + normal.x * radius * 0.04f, center.y + normal.y * radius * 0.04f, center.z};
      Vec3 right{center.x - normal.x * radius * 0.04f, center.y - normal.y * radius * 0.04f, center.z};
      AppendColoredLine(vertices, left, right, r, g, b);
    }
  }
}

Vec3 VisualEndPoint(Vec3 start, Vec3 display_delta, Vec3 fallback_delta) {
  Vec3 delta = display_delta;
  float display_length2 = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
  if (display_length2 <= 1e-8f) {
    delta = fallback_delta;
  }
  return Vec3{start.x + delta.x, start.y + delta.y, start.z + delta.z};
}

std::array<float, 16> ToColumnMajorArray(const Eigen::Matrix4f& matrix) {
  std::array<float, 16> out{};
  for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 4; ++col) {
      out[static_cast<size_t>(col) * 4u + static_cast<size_t>(row)] = matrix(row, col);
    }
  }
  return out;
}

}  // namespace

VulkanRenderer::VulkanRenderer(const WindowHandle& window) {
  window_ = window;
  CreateInstanceAndDevice(window);
  CreateAllocator();
  CreateSwapchain();
  CreatePipeline();
  CreateFrameResources();
  CreateImGui(window);
  CreateVertexBuffer(256 * sizeof(GpuVertex));
  CreateSceneTarget(swapchain_extent_);
}

VulkanRenderer::~VulkanRenderer() {
  if (device_) vkDeviceWaitIdle(device_);
  DestroySceneTarget();
  DestroyImGui();
  DestroyBuffer(vertex_buffer_);
  DestroyFrameResources();
  DestroyPipeline();
  DestroySwapchain();
  if (allocator_) vmaDestroyAllocator(allocator_);
  if (device_) vkDestroyDevice(device_, nullptr);
  if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
  if (instance_) vkDestroyInstance(instance_, nullptr);
}

void VulkanRenderer::CreateInstanceAndDevice(const WindowHandle& window) {
  uint32_t sdl_extension_count = 0;
  const char* const* sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_extension_count);
  if (!sdl_extensions || sdl_extension_count == 0) {
    throw std::runtime_error(std::string("SDL_Vulkan_GetInstanceExtensions failed: ") + SDL_GetError());
  }

  auto instance_ret = vkb::InstanceBuilder{}
    .set_app_name("Vulkan Mesh Editor")
    .request_validation_layers()
    .use_default_debug_messenger()
    .require_api_version(1, 3, 0)
    ;
  for (uint32_t i = 0; i < sdl_extension_count; ++i) {
    instance_ret.enable_extension(sdl_extensions[i]);
  }
  auto built_instance = instance_ret.build();
  if (!built_instance) throw std::runtime_error(built_instance.error().message());
  vkb::Instance vkb_instance = built_instance.value();
  instance_ = vkb_instance.instance;
  surface_ = CreateSdlSurface(instance_, window.window);

  auto physical_ret = vkb::PhysicalDeviceSelector{vkb_instance}.set_surface(surface_).select();
  if (!physical_ret) throw std::runtime_error(physical_ret.error().message());
  vkb::PhysicalDevice selected = physical_ret.value();
  physical_device_ = selected.physical_device;

  QueueFamilies queue_families = FindQueueFamilies(physical_device_, surface_);
  if (!queue_families.graphics || !queue_families.present) throw std::runtime_error("No required queue family found");
  graphics_queue_family_ = queue_families.graphics.value();
  present_queue_family_ = queue_families.present.value();

  float priority = 1.0f;
  std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
  std::vector<uint32_t> families = {graphics_queue_family_};
  if (present_queue_family_ != graphics_queue_family_) families.push_back(present_queue_family_);
  for (uint32_t family : families) {
    VkDeviceQueueCreateInfo queue{};
    queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue.queueFamilyIndex = family;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;
    queue_create_infos.push_back(queue);
  }

  const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  VkPhysicalDeviceFeatures features{};
  VkPhysicalDeviceVulkan13Features features13{};
  features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
  features13.dynamicRendering = VK_TRUE;
  VkDeviceCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  info.pNext = &features13;
  info.queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size());
  info.pQueueCreateInfos = queue_create_infos.data();
  info.pEnabledFeatures = &features;
  info.enabledExtensionCount = 1;
  info.ppEnabledExtensionNames = extensions;

  if (vkCreateDevice(physical_device_, &info, nullptr, &device_) != VK_SUCCESS) {
    throw std::runtime_error("vkCreateDevice failed");
  }
  vkGetDeviceQueue(device_, graphics_queue_family_, 0, &graphics_queue_);
  vkGetDeviceQueue(device_, present_queue_family_, 0, &present_queue_);
}

void VulkanRenderer::CreateAllocator() {
  VmaAllocatorCreateInfo info{};
  info.instance = instance_;
  info.physicalDevice = physical_device_;
  info.device = device_;
  info.vulkanApiVersion = VK_API_VERSION_1_3;
  if (vmaCreateAllocator(&info, &allocator_) != VK_SUCCESS) {
    throw std::runtime_error("vmaCreateAllocator failed");
  }
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
  VkPushConstantRange push_constant{};
  push_constant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  push_constant.offset = 0;
  push_constant.size = sizeof(float) * 16;

  VkPipelineLayoutCreateInfo layout{};
  layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layout.pushConstantRangeCount = 1;
  layout.pPushConstantRanges = &push_constant;
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

  VkPipelineViewportStateCreateInfo viewport_state{};
  viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport_state.viewportCount = 1;
  viewport_state.scissorCount = 1;
  VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamic{};
  dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic.dynamicStateCount = static_cast<uint32_t>(std::size(dynamic_states));
  dynamic.pDynamicStates = dynamic_states;

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
  info.pDynamicState = &dynamic;
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

void VulkanRenderer::CreateImGui(const WindowHandle& window) {
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
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  imgui_ini_path_ = ResolveImGuiIniPath();
  io.IniFilename = imgui_ini_path_.c_str();
  ImGui::StyleColorsDark();

  ImGui_ImplSDL3_InitForVulkan(window.window);

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
  VmaAllocationCreateInfo alloc_info{};
  alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
  alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
  if (vmaCreateBuffer(allocator_, &buffer, &alloc_info, &vertex_buffer_.buffer, &vertex_buffer_.allocation, &vertex_buffer_.allocation_info) != VK_SUCCESS) {
    throw std::runtime_error("vmaCreateBuffer failed");
  }
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

void VulkanRenderer::UpdateVertexBuffer(const Mesh& mesh, const RenderUiState& ui_state, uint32_t& green_hatch_count, uint32_t& yellow_hatch_count, uint32_t& red_hatch_count, uint32_t& line_count, uint32_t& directive_line_count, uint32_t& point_count) {
  std::vector<GpuVertex> vertices;
  vertices.reserve(mesh.edges.size() * 2 + mesh.positions.size() + mesh.triangles.size() * 80);
  std::vector<bool> active_vertices(mesh.positions.size(), false);
  for (uint32_t tri_index = 0; tri_index < mesh.triangles.size(); ++tri_index) {
    AppendHatchForTriangle(vertices, mesh, tri_index, 0.0f, 0.75f, 0.25f);
  }
  green_hatch_count = static_cast<uint32_t>(vertices.size());
  yellow_hatch_count = 0;
  red_hatch_count = 0;

  auto is_active_edge = [&](const std::array<uint32_t, 2>& edge) {
    return edge[0] < active_vertices.size() && edge[1] < active_vertices.size() && (active_vertices[edge[0]] || active_vertices[edge[1]]);
  };

  uint32_t line_start = static_cast<uint32_t>(vertices.size());
  for (uint32_t edge_index = 0; edge_index < mesh.edges.size(); ++edge_index) {
    const auto& edge = mesh.edges[edge_index];
    bool active = is_active_edge(edge);
    float r = active ? 0.25f : 0.0f;
    float g = active ? 0.65f : 0.9f;
    float b = active ? 1.0f : 0.25f;
    for (uint32_t index : edge) {
      const Vec3& p = mesh.positions[index];
      vertices.push_back({p.x, p.y, p.z, r, g, b});
    }
  }
  line_count = static_cast<uint32_t>(vertices.size()) - line_start;

  directive_line_count = 0u;

  for (uint32_t i = 0; i < mesh.positions.size(); ++i) {
    const Vec3& p = mesh.positions[i];
    bool active = i < active_vertices.size() && active_vertices[i];
    float r = active ? 0.25f : 0.0f;
    float g = active ? 0.65f : 0.9f;
    float b = active ? 1.0f : 0.25f;
    vertices.push_back({p.x, p.y, p.z, r, g, b});
  }
  point_count = static_cast<uint32_t>(mesh.positions.size());

  size_t bytes = vertices.size() * sizeof(GpuVertex);
  EnsureVertexBufferSize(bytes);

  if (bytes > 0 && vertex_buffer_.allocation_info.pMappedData) {
    std::memcpy(vertex_buffer_.allocation_info.pMappedData, vertices.data(), bytes);
  }
  (void)ui_state;
}

void VulkanRenderer::CreateSceneTarget(VkExtent2D extent) {
  if (extent.width == 0 || extent.height == 0) {
    extent = swapchain_extent_;
  }
  DestroySceneTarget();

  VkImageCreateInfo image_info{};
  image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_info.imageType = VK_IMAGE_TYPE_2D;
  image_info.format = swapchain_format_;
  image_info.extent = {extent.width, extent.height, 1};
  image_info.mipLevels = 1;
  image_info.arrayLayers = 1;
  image_info.samples = VK_SAMPLE_COUNT_1_BIT;
  image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VmaAllocationCreateInfo allocation_info{};
  allocation_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
  if (vmaCreateImage(allocator_, &image_info, &allocation_info, &scene_target_.image, &scene_target_.allocation, nullptr) != VK_SUCCESS) {
    throw std::runtime_error("vmaCreateImage for scene target failed");
  }

  VkImageViewCreateInfo view_info{};
  view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view_info.image = scene_target_.image;
  view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view_info.format = swapchain_format_;
  view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  view_info.subresourceRange.levelCount = 1;
  view_info.subresourceRange.layerCount = 1;
  if (vkCreateImageView(device_, &view_info, nullptr, &scene_target_.view) != VK_SUCCESS) {
    throw std::runtime_error("vkCreateImageView for scene target failed");
  }

  scene_target_.descriptor = ImGui_ImplVulkan_AddTexture(scene_target_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  scene_target_.extent = extent;
  scene_target_recreate_pending_ = false;
  scene_target_pending_extent_ = extent;
  scene_target_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
}

void VulkanRenderer::DestroySceneTarget() {
  if (scene_target_.descriptor) {
    ImGui_ImplVulkan_RemoveTexture(scene_target_.descriptor);
    scene_target_.descriptor = VK_NULL_HANDLE;
  }
  if (scene_target_.view) vkDestroyImageView(device_, scene_target_.view, nullptr);
  if (scene_target_.image) vmaDestroyImage(allocator_, scene_target_.image, scene_target_.allocation);
  scene_target_ = {};
  scene_target_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
  scene_view_bounds_ = {};
}

void VulkanRenderer::RequestSceneTargetResize(VkExtent2D extent) {
  if (extent.width == 0 || extent.height == 0) return;
  if (scene_target_.extent.width == extent.width && scene_target_.extent.height == extent.height) return;
  scene_target_recreate_pending_ = true;
  scene_target_pending_extent_ = extent;
}

void VulkanRenderer::RenderScene(VkCommandBuffer cmd, const Mesh& mesh, const SceneCamera& camera, const RenderUiState& ui_state, uint32_t green_hatch_count, uint32_t yellow_hatch_count, uint32_t red_hatch_count, uint32_t line_count, uint32_t directive_line_count, uint32_t point_count, VkExtent2D extent) {
  if (scene_target_.image == VK_NULL_HANDLE || extent.width == 0 || extent.height == 0) return;

  TransitionImage(cmd, scene_target_.image, scene_target_layout_, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

  VkClearValue clear{};
  clear.color = {{0.02f, 0.02f, 0.04f, 1.0f}};
  VkRenderingAttachmentInfo color_attachment{};
  color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  color_attachment.imageView = scene_target_.view;
  color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color_attachment.clearValue = clear;

  VkRenderingInfo rendering{};
  rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  rendering.renderArea.extent = extent;
  rendering.layerCount = 1;
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachments = &color_attachment;
  vkCmdBeginRendering(cmd, &rendering);

  VkViewport viewport{};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = static_cast<float>(extent.width);
  viewport.height = static_cast<float>(extent.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  VkRect2D scissor{};
  scissor.extent = extent;
  vkCmdSetViewport(cmd, 0, 1, &viewport);
  vkCmdSetScissor(cmd, 0, 1, &scissor);

  VkDeviceSize offset = 0;
  vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer_.buffer, &offset);
  const std::array<float, 16> mvp = ToColumnMajorArray(camera.MvpMatrix());
  vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mvp), mvp.data());
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

  vkCmdEndRendering(cmd);
  TransitionImage(cmd, scene_target_.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  scene_target_layout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  (void)mesh;
  (void)ui_state;
}

RenderFrameResult VulkanRenderer::Draw(const Mesh& mesh, const SceneCamera& camera, const RenderUiState& ui_state) {
  uint32_t green_hatch_count = 0;
  uint32_t yellow_hatch_count = 0;
  uint32_t red_hatch_count = 0;
  uint32_t line_count = 0;
  uint32_t directive_line_count = 0;
  uint32_t point_count = 0;
  UpdateVertexBuffer(mesh, ui_state, green_hatch_count, yellow_hatch_count, red_hatch_count, line_count, directive_line_count, point_count);
  FrameStats stats{};
  stats.draw_calls = 6;
  bool phys_reset_requested = false;
  bool phys_step_requested = false;

  VkResult fence_wait = vkWaitForFences(device_, 1, &in_flight_, VK_TRUE, kFrameTimeoutNs);
  if (fence_wait == VK_TIMEOUT) throw std::runtime_error("Timed out waiting for GPU fence");
  if (fence_wait != VK_SUCCESS) throw std::runtime_error("vkWaitForFences failed");
  vkResetFences(device_, 1, &in_flight_);

  if (scene_target_recreate_pending_) {
    CreateSceneTarget(scene_target_pending_extent_);
  }

  uint32_t image_index = 0;
  VkResult acquire = vkAcquireNextImageKHR(device_, swapchain_, kFrameTimeoutNs, image_available_, VK_NULL_HANDLE, &image_index);
  if (acquire == VK_TIMEOUT) throw std::runtime_error("Timed out waiting for swapchain image");
  if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) throw std::runtime_error("vkAcquireNextImageKHR failed");

  VkCommandBuffer cmd = command_buffers_[image_index];
  vkResetCommandBuffer(cmd, 0);
  VkCommandBufferBeginInfo begin{};
  begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  vkBeginCommandBuffer(cmd, &begin);

  RenderScene(cmd, mesh, camera, ui_state, green_hatch_count, yellow_hatch_count, red_hatch_count, line_count, directive_line_count, point_count, scene_target_.extent);

  TransitionImage(cmd, swapchain_images_[image_index], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

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

  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
  ImGuiIO& io = ImGui::GetIO();
  ImGuiViewport* main_viewport = ImGui::GetMainViewport();
  constexpr float kToolbarHeight = 44.0f;
  const float toolbar_height = show_toolbar_ ? kToolbarHeight : 0.0f;

  if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_T, false)) {
    show_toolbar_ = !show_toolbar_;
  }

  if (show_toolbar_) {
    ImGuiWindowFlags toolbar_flags =
      ImGuiWindowFlags_NoDecoration |
      ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoBringToFrontOnFocus |
      ImGuiWindowFlags_NoNav |
      ImGuiWindowFlags_NoDocking;
    ImGui::SetNextWindowPos(main_viewport->Pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(main_viewport->Size.x, kToolbarHeight), ImGuiCond_Always);
    ImGui::SetNextWindowViewport(main_viewport->ID);
    ImGui::Begin("Toolbar", nullptr, toolbar_flags);
    ImGui::TextUnformatted("Pages");
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Show or hide the editor pages");
    }
    ImGui::SameLine();
    if (ImGui::Button("Toggle")) {
      ImGui::OpenPopup("Page Visibility");
    }
    if (ImGui::BeginPopup("Page Visibility")) {
      ImGui::MenuItem("Phys", nullptr, &show_phys_window_);
      ImGui::MenuItem("Animation", nullptr, &show_animation_window_);
      ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::Text("mode: %s", mode_ == InteractionMode::Edit ? "edit" : "phys");
    ImGui::SameLine();
    ImGui::Text("run: %s", phys_run_state_ == PhysRunState::Run ? "run" : "pause");
    ImGui::SameLine();
    ImGui::TextDisabled("Ctrl+T");
    ImGui::End();
  }

  ImGuiWindowFlags host_flags =
    ImGuiWindowFlags_NoDecoration |
    ImGuiWindowFlags_NoMove |
    ImGuiWindowFlags_NoSavedSettings |
    ImGuiWindowFlags_NoBringToFrontOnFocus |
    ImGuiWindowFlags_NoNav |
    ImGuiWindowFlags_NoBackground |
    ImGuiWindowFlags_NoDocking;
  ImGui::SetNextWindowPos(ImVec2(main_viewport->Pos.x, main_viewport->Pos.y + toolbar_height), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(main_viewport->Size.x, std::max(0.0f, main_viewport->Size.y - toolbar_height)), ImGuiCond_Always);
  ImGui::SetNextWindowViewport(main_viewport->ID);
  ImGui::Begin("DockHost", nullptr, host_flags);
  uint32_t dockspace_id = ImGui::GetID("MainDockSpace");
  if (!dock_layout_initialized_) {
    SetupDockLayout(dockspace_id, main_viewport->Size.x, std::max(0.0f, main_viewport->Size.y - toolbar_height));
  }
  ImGui::DockSpace(static_cast<ImGuiID>(dockspace_id), ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
  ImGui::End();

  const int current_frame_index = ui_state.phys_current_frame_index;

  auto draw_phys_window = [&]() {
    if (!show_phys_window_) return;
    ImGui::Begin("Phys");
    ImGui::Text("drawcall: %u", stats.draw_calls);
    ImGui::Text("current frame: %d", current_frame_index);
    ImGui::Text("solver: %s", ui_state.phys_solver_kind == PhysSolverKind::Gpu ? "gpu" : "cpu");
    ImGui::Text("algorithm: %s", ui_state.phys_algorithm_name.empty() ? "<none>" : ui_state.phys_algorithm_name.c_str());
    if (ui_state.gpu_dispatch_debug.valid) {
      const GpuPhysicsDispatchDebugInfo& debug = ui_state.gpu_dispatch_debug;
      ImGui::Text("gpu shader: %s", debug.shader_name.c_str());
      ImGui::Text("gpu demo size: %ux%u", debug.width, debug.height);
      if (!debug.values.empty()) {
        ImGui::Text("gpu sample[0]: %.3f", debug.values.front());
      }
    }
    ImGui::Spacing();
    ImGui::TextUnformatted("simulation");
    if (ImGui::Button("Start")) {
      mode_ = InteractionMode::Phys;
      phys_run_state_ = PhysRunState::Run;
    }
    ImGui::SameLine();
    if (ImGui::Button("Pause")) {
      mode_ = InteractionMode::Phys;
      phys_run_state_ = PhysRunState::Pause;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
      mode_ = InteractionMode::Phys;
      phys_reset_requested = true;
      phys_run_state_ = PhysRunState::Pause;
    }
    ImGui::SameLine();
    if (phys_run_state_ == PhysRunState::Pause) {
      if (ImGui::Button("Single Frame")) {
        mode_ = InteractionMode::Phys;
        phys_step_requested = true;
      }
    } else {
      ImGui::BeginDisabled();
      ImGui::Button("Single Frame");
      ImGui::EndDisabled();
    }
    ImGui::Text("mode: %s", mode_ == InteractionMode::Phys ? "phys" : "edit");
    ImGui::Text("run state: %s", phys_run_state_ == PhysRunState::Run ? "run" : "pause");

    ImGui::End();
  };

  auto draw_animation_window = [&]() {
    if (!show_animation_window_) return;
    ImGui::Begin("Animation");
    if (ImGui::IsWindowCollapsed()) {
      ImGui::TextDisabled("Animation view is collapsed");
      ImGui::End();
      return;
    }
    ImVec2 scene_avail = ImGui::GetContentRegionAvail();
    if (scene_avail.x < 20.0f) scene_avail.x = 20.0f;
    if (scene_avail.y < 20.0f) scene_avail.y = 20.0f;
    VkExtent2D requested_extent{static_cast<uint32_t>(scene_avail.x), static_cast<uint32_t>(scene_avail.y)};
    RequestSceneTargetResize(requested_extent);
    ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(scene_target_.descriptor)), scene_avail);
    ImVec2 scene_min = ImGui::GetItemRectMin();
    ImVec2 scene_max = ImGui::GetItemRectMax();
    scene_view_bounds_.x = scene_min.x;
    scene_view_bounds_.y = scene_min.y;
    scene_view_bounds_.width = scene_max.x - scene_min.x;
    scene_view_bounds_.height = scene_max.y - scene_min.y;
    scene_view_bounds_.valid = scene_view_bounds_.width > 0.0f && scene_view_bounds_.height > 0.0f;
    ImGui::End();
  };

  draw_phys_window();
  draw_animation_window();

  ImGui::Render();
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

  vkCmdEndRendering(cmd);
  TransitionImage(cmd, swapchain_images_[image_index], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
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
  RenderFrameResult result{};
  result.draw_calls = stats.draw_calls;
  result.mode = mode_;
  result.phys_run_state = phys_run_state_;
  result.phys_reset_requested = phys_reset_requested;
  result.phys_step_requested = phys_step_requested;
  return result;
}

void VulkanRenderer::SetupDockLayout(uint32_t dockspace_id, float width, float height) {
  ImGui::DockBuilderRemoveNode(static_cast<ImGuiID>(dockspace_id));
  ImGui::DockBuilderAddNode(static_cast<ImGuiID>(dockspace_id), ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(static_cast<ImGuiID>(dockspace_id), ImVec2(width, height));

  ImGuiID dock_bottom = 0;
  ImGuiID dock_top = ImGui::DockBuilderSplitNode(static_cast<ImGuiID>(dockspace_id), ImGuiDir_Down, 0.80f, nullptr, &dock_bottom);
  ImGui::DockBuilderDockWindow("Animation", dock_top);
  ImGui::DockBuilderDockWindow("Phys", dock_bottom);
  ImGui::DockBuilderFinish(static_cast<ImGuiID>(dockspace_id));
  dock_layout_initialized_ = true;
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

void VulkanRenderer::TransitionImage(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout) {
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
  } else if (old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
  } else if (old_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
    barrier.dstAccessMask = 0;
  } else if (old_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
  }

  VkDependencyInfo dependency{};
  dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  dependency.imageMemoryBarrierCount = 1;
  dependency.pImageMemoryBarriers = &barrier;
  vkCmdPipelineBarrier2(cmd, &dependency);
}

void VulkanRenderer::DestroyBuffer(Buffer& buffer) {
  if (buffer.buffer) vmaDestroyBuffer(allocator_, buffer.buffer, buffer.allocation);
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
  ImGui_ImplSDL3_Shutdown();
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

}  // namespace service_domains
