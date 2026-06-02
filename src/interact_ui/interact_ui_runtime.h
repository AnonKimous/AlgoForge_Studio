#pragma once

#include "agent_execute/agent_execute_runtime.h"

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
  void DrawLoadedAgentListUi();
  void DrawAgentDraftUi();
  void DrawInteractUi();
  bool LoadMeshAndResetBindings(const std::string& path, std::string* status_message);
  bool CreateAgentFromDraft(std::string* status_message);

  agent_execute::AgentExecuteRuntime execute_runtime_{};
  AgentDraftState agent_draft_{};
  std::size_t selected_agent_slot_{static_cast<std::size_t>(-1)};
};

}  // namespace interact_ui

using interact_ui::InteractUiRuntime;
