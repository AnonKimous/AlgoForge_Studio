#pragma once

#include "../interaction/interaction_state.h"
#include "../mesh.h"
#include "../triangle_orientation_analyzer.h"
#include "../win32_window.h"

#include <vulkan/vulkan.h>
#include <vkb/VkBootstrap.h>

#include <cstdint>
#include <vector>

struct FrameStats {
  uint32_t draw_calls{};
  uint32_t line_vertices{};
  uint32_t point_vertices{};
};

struct GpuVertex {
  float px, py, pz;
  float r, g, b;
};

class VulkanRenderer {
 public:
  explicit VulkanRenderer(const Win32Window& window);
  ~VulkanRenderer();

  RenderFrameResult Draw(const Mesh& mesh, const TriangleOrientationAnalyzer& analyzer, int highlighted_vertex, const RenderUiState& ui_state);
  InteractionMode mode() const { return mode_; }
  PhysSubMode phys_sub_mode() const { return phys_sub_mode_; }

 private:
  struct Buffer {
    VkBuffer buffer{};
    VkDeviceMemory memory{};
    size_t size{};
  };

  void CreateInstanceAndDevice(const Win32Window& window);
  void CreateSwapchain();
  void CreatePipeline();
  void CreateFrameResources();
  void CreateImGui(const Win32Window& window);
  void CreateVertexBuffer(size_t size);
  void EnsureVertexBufferSize(size_t size);
  void UpdateVertexBuffer(const Mesh& mesh, const TriangleOrientationAnalyzer& analyzer, int highlighted_vertex, const RenderUiState& ui_state, uint32_t& green_hatch_count, uint32_t& yellow_hatch_count, uint32_t& red_hatch_count, uint32_t& line_count, uint32_t& directive_line_count, uint32_t& point_count);
  void AppendHudVertices(std::vector<GpuVertex>& vertices, const FrameStats& stats);
  void DestroyFrameResources();
  void DestroyImGui();
  void DestroySwapchain();
  void DestroyPipeline();
  void DestroyBuffer(Buffer& buffer);

  uint32_t FindMemoryType(uint32_t type_filter, VkMemoryPropertyFlags properties) const;
  VkShaderModule CreateShaderModule(const char* path) const;
  VkPipeline CreateGraphicsPipeline(VkPrimitiveTopology topology, float point_size);
  void TransitionSwapchainImage(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout);

  VkInstance instance_{};
  VkSurfaceKHR surface_{};
  VkPhysicalDevice physical_device_{};
  VkDevice device_{};
  VkQueue graphics_queue_{};
  VkQueue present_queue_{};
  uint32_t graphics_queue_family_{};
  uint32_t present_queue_family_{};

  VkSwapchainKHR swapchain_{};
  VkFormat swapchain_format_{};
  VkExtent2D swapchain_extent_{};
  std::vector<VkImage> swapchain_images_;
  std::vector<VkImageView> swapchain_image_views_;

  VkPipelineLayout pipeline_layout_{};
  VkPipeline line_pipeline_{};
  VkPipeline point_pipeline_{};

  VkCommandPool command_pool_{};
  std::vector<VkCommandBuffer> command_buffers_;
  VkSemaphore image_available_{};
  VkSemaphore render_finished_{};
  VkFence in_flight_{};
  Buffer vertex_buffer_{};
  VkDescriptorPool imgui_descriptor_pool_{};
  InteractionMode mode_{InteractionMode::Edit};
  PhysSubMode phys_sub_mode_{PhysSubMode::Guide};
};
