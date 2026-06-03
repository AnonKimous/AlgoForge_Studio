#pragma once

#include "common_data/input_state.h"
#include "common_data/mesh.h"
#include "runtime_systems/render/imgui_vulkan_runtime.h"
#include "runtime_systems/window/window.h"

#include <array>
#include <memory>
#include <utility>
#include <vector>

namespace agent_execute {

void DrawVertexArrayOverlay(
  const std::vector<Vec3>& vertex_positions,
  const std::vector<std::array<uint32_t, 2>>& triangle_edges,
  const std::vector<std::array<uint32_t, 3>>& triangles);

class WindowAgent {
 public:
  using DrawCallback = runtime_systems::ImGuiVulkanRuntime::DrawCallback;

  bool Init(const char* title, int width, int height);
  bool Tick();
  void Destroy();
  void SetDrawCallback(DrawCallback callback);

  WindowHandle native_handle() const;
  int width() const;
  int height() const;
  const InputState& input() const;
  Vec2 MousePosition() const;

 private:
  std::unique_ptr<SdlWindow> window_;
  std::unique_ptr<runtime_systems::ImGuiVulkanRuntime> imgui_runtime_;
};

}  // namespace agent_execute
