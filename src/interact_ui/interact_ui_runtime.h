#pragma once

#include "agent_execute/agent_execute_runtime.h"
#include "agents/agents.h"
#include "common_data/mesh.h"

#include <chrono>
#include <cstddef>

namespace interact_ui {

class InteractUiRuntime {
 public:
  bool Init(const Mesh& mesh, const char* window_title, int width, int height);
  bool LoadMeshFromFile(const std::string& path, std::string* error_message = nullptr);
  bool Tick();
  void Destroy();

 private:
  struct AgentDraftState {
    char agent_name[128]{};
    char algorithm_name[128]{};
    char mesh_path[512]{};
    char gpu_shader_path[512]{};
    int preset_index{1};
    int solver_kind_index{0};
    bool load_mesh_before_create{true};
  };

  void ResetAgentDraftState();
  void DrawLoadedAgentUi();
  void DrawAgentDraftUi();
  void DrawInteractUi();
  bool LoadMeshAndResetState(const std::string& path, std::string* status_message);
  bool CreateAgentFromDraft(std::string* status_message);

  Mesh mesh_{};
  std::string mesh_source_path_{};
  agent_execute::AgentExecuteRuntime execute_runtime_{};
  agent_execute::WindowAgent window_agent_{};
  AgentDraftState agent_draft_{};
  std::string ui_status_message_{};
  std::size_t current_agent_id_{0};
  std::chrono::steady_clock::time_point last_frame_time_{};
  float frame_dt_{0.0f};
};

}  // namespace interact_ui

using interact_ui::InteractUiRuntime;
