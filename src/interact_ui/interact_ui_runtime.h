#pragma once

#include "agent/agent.h"
#include "agent_execute/agent_execute_runtime.h"
#include "common_data/mesh.h"
#include "runtime_systems/runtime_environment.h"

#include <chrono>
#include <memory>
#include <string>

namespace interact_ui {

class InteractUiRuntime {
 public:
  bool Init(const Mesh& mesh, const char* window_title, int width, int height);
  bool Tick();
  void Destroy();

 private:
  void DrawInteractUi();
  void DrawAgentBindingUi();
  void DrawCustomInterventionUi();
  void SyncCustomInterventionUiState();

  Mesh mesh_{};
  agent_execute::AgentExecuteRuntime execute_runtime_{};
  runtime_systems::RuntimeEnvironment runtime_environment_{};
  std::string ui_status_message_{};
  std::shared_ptr<agent::IAlgorithmInterventionPackageUi> active_custom_ui_hook_{};
  std::unique_ptr<agent::IAlgorithmInterventionPackageUiState> active_custom_ui_state_{};
  std::chrono::steady_clock::time_point last_frame_time_{};
  float frame_dt_{0.0f};
};

}  // namespace interact_ui

using interact_ui::InteractUiRuntime;
