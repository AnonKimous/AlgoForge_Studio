#pragma once

#include "agent_execute/agent_execute_runtime.h"
#include "common_data/mesh.h"
#include "runtime_systems/render/imgui_vulkan_runtime.h"
#include "runtime_systems/window/sdl_window.h"

#include <chrono>
#include <memory>

namespace interact_ui {

class InteractUiRuntime {
 public:
  bool Init(const Mesh& mesh, const char* window_title, int width, int height);
 bool LoadMeshFromFile(const std::string& path, std::string* error_message = nullptr);
  bool Tick();
  void Destroy();

 private:
  void DrawLoadedAgentUi();
  void DrawMeshUi();
  void DrawInteractUi();
  bool LoadMeshAndResetState(const std::string& path, std::string* status_message);

  Mesh mesh_{};
  std::string mesh_source_path_{};
  agent_execute::AgentExecuteRuntime execute_runtime_{};
  std::unique_ptr<SdlWindow> window_{};
  std::unique_ptr<runtime_systems::ImGuiVulkanRuntime> imgui_runtime_{};
  char mesh_path_input_[512]{};
  std::string ui_status_message_{};
  std::chrono::steady_clock::time_point last_frame_time_{};
  float frame_dt_{0.0f};
};

}  // namespace interact_ui

using interact_ui::InteractUiRuntime;
