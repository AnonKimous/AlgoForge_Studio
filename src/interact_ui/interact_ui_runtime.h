#pragma once

#include "agent_management/agent_manager.h"
#include "interact_ui/interact_ui_host.h"
#include "runtime_systems/runtime_environment.h"

#include <chrono>
#include <vector>
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
  bool CreateAgent(AgentCreateSpec spec, size_t* out_agent_index = nullptr) override {
    return agent_manager_.CreateAgent(std::move(spec), out_agent_index);
  }
  bool AttachAlgorithmToAgent(
    size_t agent_index,
    const std::string& algorithm_name,
    const std::vector<agent::AlgorithmResourceBinding>& resource_bindings,
    const std::vector<agent::AlgorithmDescriptorValue>& descriptor_values,
    size_t* out_algorithm_index = nullptr,
    std::string* out_error_message = nullptr,
    agent::AlgorithmMountMode mount_mode = agent::AlgorithmMountMode::Direct,
    agent::AlgorithmExecutionPreference execution_preference = agent::AlgorithmExecutionPreference::Gpu) override {
    return agent_manager_.AttachAlgorithmToAgent(
      agent_index,
      algorithm_name,
      resource_bindings,
      descriptor_values,
      out_algorithm_index,
      out_error_message,
      mount_mode,
      execution_preference);
  }
  bool TickManagedAgents() override {
    return agent_manager_.Tick(runtime_environment_.input(), runtime_environment_.MousePosition(), frame_dt_);
  }
  void ClearAgents() override {
    agent_manager_.ClearAgents();
  }
  void ClearGpuExecutors() override {
    runtime_environment_.ClearGpuExecutors();
  }
  const InputState& input() const override { return runtime_environment_.input(); }
  Vec2 mouse_position() const override { return runtime_environment_.MousePosition(); }
  float frame_dt_seconds() const override { return frame_dt_; }
  bool has_render_preview_texture() const override { return runtime_environment_.HasRenderPreviewTexture(); }
  ImTextureID render_preview_texture_id() const override { return runtime_environment_.RenderPreviewTextureId(); }
  ImVec2 render_preview_texture_size() const override { return runtime_environment_.RenderPreviewTextureSize(); }
  void SetRenderPreviewExtent(ImVec2 extent) override {
    runtime_environment_.SetRenderPreviewExtent(extent);
  }
  void SetRenderPreviewRequest(runtime_systems::RenderPreviewRequest request) override {
    render_preview_request_ = std::move(request);
    runtime_environment_.SetRenderPreviewRequest(render_preview_request_);
  }
  std::string& ui_status_message() override { return ui_status_message_; }
  const std::string& ui_status_message() const override { return ui_status_message_; }
  runtime_systems::RuntimeEnvironment& runtime_environment() { return runtime_environment_; }
  const runtime_systems::RuntimeEnvironment& runtime_environment() const { return runtime_environment_; }

 private:
  runtime_systems::RuntimeEnvironment runtime_environment_{};
  AgentManager agent_manager_{};
  runtime_systems::RenderPreviewRequest render_preview_request_{};
  std::string ui_status_message_{};
  std::chrono::steady_clock::time_point last_frame_time_{};
  float frame_dt_{0.0f};
};

}  // namespace interact_ui

using interact_ui::InteractUiRuntime;
