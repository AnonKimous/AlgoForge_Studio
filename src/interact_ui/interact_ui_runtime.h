#pragma once

#include "agent_management/agent_manager.h"
#include "interact_ui/interact_ui_host.h"
#include "runtime_systems/runtime_environment.h"

#include <chrono>
#include <string>
#include <utility>

namespace interact_ui {

class InteractUiRuntime : public IInteractUiHost {
 public:
  ~InteractUiRuntime();

  bool Init(const char* window_title, int width, int height);
  bool Tick();
  void Destroy();

  AgentManager& agent_manager() override { return agent_manager_; }
  const AgentManager& agent_manager() const override { return agent_manager_; }
  const InputState& input() const override { return runtime_environment_.input(); }
  Vec2 mouse_position() const override { return runtime_environment_.MousePosition(); }
  float frame_dt_seconds() const override { return frame_dt_; }
  void SetRenderPreviewRequest(runtime_systems::RenderPreviewRequest request) override {
    render_preview_request_ = std::move(request);
    runtime_environment_.SetRenderPreviewRequest(render_preview_request_);
  }
  std::string& ui_status_message() override { return ui_status_message_; }
  const std::string& ui_status_message() const override { return ui_status_message_; }
  runtime_systems::RuntimeEnvironment& runtime_environment() { return runtime_environment_; }
  const runtime_systems::RuntimeEnvironment& runtime_environment() const { return runtime_environment_; }

 private:
  AgentManager agent_manager_{};
  runtime_systems::RuntimeEnvironment runtime_environment_{};
  runtime_systems::RenderPreviewRequest render_preview_request_{};
  std::string ui_status_message_{};
  std::chrono::steady_clock::time_point last_frame_time_{};
  float frame_dt_{0.0f};
};

}  // namespace interact_ui

using interact_ui::InteractUiRuntime;
