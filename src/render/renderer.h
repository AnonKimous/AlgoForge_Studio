#pragma once

#include "../interaction/interaction_state.h"
#include "../mesh.h"
#include "../triangle_orientation_analyzer.h"
#include "window/window_handle.h"

#include <vulkan/vulkan.h>
#include <vkb/VkBootstrap.h>

#include <cstdint>
#include <vector>

struct FrameStats {
  uint32_t draw_calls{};
  uint32_t line_vertices{};
  uint32_t point_vertices{};
};

struct SceneViewBounds {
  float x{};
  float y{};
  float width{};
  float height{};
  bool valid{false};
};

struct GpuVertex {
  float px, py, pz;
  float r, g, b;
};

class VulkanRenderer {
 public:
  explicit VulkanRenderer(const WindowHandle& window);
  ~VulkanRenderer();

  RenderFrameResult Draw(const Mesh& mesh, const TriangleOrientationAnalyzer& analyzer, int highlighted_vertex, const RenderUiState& ui_state);
  InteractionMode mode() const { return mode_; }
  PhysRunState phys_run_state() const { return phys_run_state_; }
  bool phys_guide_enabled() const { return phys_guide_enabled_; }
  SceneViewBounds scene_view_bounds() const { return scene_view_bounds_; }

 private:
  struct Buffer {
    VkBuffer buffer{};
    VkDeviceMemory memory{};
    size_t size{};
  };

  struct SceneTarget {
    VkImage image{};
    VkDeviceMemory memory{};
    VkImageView view{};
    VkDescriptorSet descriptor{};
    VkExtent2D extent{};
  };

  void CreateInstanceAndDevice(const WindowHandle& window);
  void CreateSwapchain();
  void CreatePipeline();
  void CreateFrameResources();
  void CreateImGui(const WindowHandle& window);
  void CreateSceneTarget(VkExtent2D extent);
  void DestroySceneTarget();
  void RequestSceneTargetResize(VkExtent2D extent);
  void CreateVertexBuffer(size_t size);
  void EnsureVertexBufferSize(size_t size);
  void UpdateVertexBuffer(const Mesh& mesh, const TriangleOrientationAnalyzer& analyzer, int highlighted_vertex, const RenderUiState& ui_state, uint32_t& green_hatch_count, uint32_t& yellow_hatch_count, uint32_t& red_hatch_count, uint32_t& line_count, uint32_t& directive_line_count, uint32_t& point_count);
  void RenderScene(VkCommandBuffer cmd, const Mesh& mesh, const TriangleOrientationAnalyzer& analyzer, const RenderUiState& ui_state, uint32_t green_hatch_count, uint32_t yellow_hatch_count, uint32_t red_hatch_count, uint32_t line_count, uint32_t directive_line_count, uint32_t point_count, VkExtent2D extent);
  void AppendHudVertices(std::vector<GpuVertex>& vertices, const FrameStats& stats);
  void DestroyFrameResources();
  void DestroyImGui();
  void DestroySwapchain();
  void DestroyPipeline();
  void DestroyBuffer(Buffer& buffer);
  void SetupDockLayout(uint32_t dockspace_id, float width, float height);
  void TransitionImage(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout);

  uint32_t FindMemoryType(uint32_t type_filter, VkMemoryPropertyFlags properties) const;
  VkShaderModule CreateShaderModule(const char* path) const;
  VkPipeline CreateGraphicsPipeline(VkPrimitiveTopology topology, float point_size);

  WindowHandle window_{};
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
  SceneTarget scene_target_{};
  SceneViewBounds scene_view_bounds_{};
  InteractionMode mode_{InteractionMode::Edit};
  PhysRunState phys_run_state_{PhysRunState::Stop};
  bool phys_guide_enabled_{true};
  int selected_phys_directive_{-1};
  float triangle_material_input_gpa_{0.0f};
  int triangle_material_input_triangle_{-1};
  bool dock_layout_initialized_{false};
  bool show_edit_data_window_{true};
  bool show_frames_window_{true};
  bool show_phys_window_{true};
  bool show_animation_window_{true};
  bool show_toolbar_{true};
  std::string imgui_ini_path_;
  bool scene_target_recreate_pending_{false};
  VkExtent2D scene_target_pending_extent_{};
  VkImageLayout scene_target_layout_{VK_IMAGE_LAYOUT_UNDEFINED};
  bool swapchain_recreate_pending_{false};
  VkExtent2D swapchain_pending_extent_{};
};
