#include "interact_ui_runtime.h"

#include <chrono>

namespace interact_ui {

InteractUiRuntime::~InteractUiRuntime() {
  Destroy();
}

bool InteractUiRuntime::Init(const char* window_title, int width, int height) {
  agent_manager_.Destroy();
  ui_status_message_.clear();
  if (!runtime_environment_.Init(window_title ? window_title : "debugTool", width, height)) {
    return false;
  }
  AgentCreateSpec default_agent_spec{};
  default_agent_spec.agent_name = "debug_agent";
  default_agent_spec.limit_fps_flag = 120u;
  if (!agent_manager_.CreateAgent(std::move(default_agent_spec))) {
    runtime_environment_.Destroy();
    return false;
  }
  agent_manager_.PauseTicking();
  frame_dt_ = 0.0f;
  last_frame_time_ = std::chrono::steady_clock::now();
  return true;
}

bool InteractUiRuntime::Tick() {
  if (!runtime_environment_.has_window()) {
    return false;
  }

  const auto now = std::chrono::steady_clock::now();
  frame_dt_ = std::chrono::duration<float>(now - last_frame_time_).count();
  last_frame_time_ = now;

  return runtime_environment_.Tick();
}

void InteractUiRuntime::Destroy() {
  agent_manager_.Destroy();
  runtime_environment_.Destroy();
  ui_status_message_.clear();
  last_frame_time_ = {};
  frame_dt_ = 0.0f;
}

}  // namespace interact_ui
