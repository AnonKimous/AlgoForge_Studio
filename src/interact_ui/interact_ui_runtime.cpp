#include "interact_ui_runtime.h"

#include <chrono>
#include <utility>

namespace interact_ui {

InteractUiRuntime::~InteractUiRuntime() = default;

bool InteractUiRuntime::Init(const char* window_title, int width, int height) {
  agent_manager_.Destroy();
  ui_panel_.Destroy();
  ui_status_message_.clear();
  if (!runtime_environment_.Init(window_title ? window_title : "Interact & UI", width, height)) {
    return false;
  }
  frame_dt_ = 0.0f;
  last_frame_time_ = std::chrono::steady_clock::now();
  return true;
}

bool InteractUiRuntime::CreateAgent(
  AgentCreateSpec spec,
  std::vector<AgentInterventionUiBinding> ui_bindings) {
  size_t created_agent_index = 0u;
  if (!agent_manager_.CreateAgent(std::move(spec), &created_agent_index)) {
    ui_status_message_ = "Failed to create managed agent.";
    return false;
  }
  ui_panel_.RegisterAgentUiBindings(created_agent_index, std::move(ui_bindings));
  ui_status_message_ = "Agent shell created and registered in agent manager.";
  return true;
}

bool InteractUiRuntime::Tick() {
  if (!runtime_environment_.has_window()) {
    return false;
  }

  const auto now = std::chrono::steady_clock::now();
  frame_dt_ = std::chrono::duration<float>(now - last_frame_time_).count();
  last_frame_time_ = now;

  runtime_environment_.SetDrawCallback([this]() {
    ui_panel_.Draw(*this);
  });
  return runtime_environment_.Tick();
}

void InteractUiRuntime::Destroy() {
  agent_manager_.Destroy();
  runtime_environment_.Destroy();
  ui_panel_.Destroy();
  ui_status_message_.clear();
  last_frame_time_ = {};
  frame_dt_ = 0.0f;
}

}  // namespace interact_ui
