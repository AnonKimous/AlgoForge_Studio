#pragma once

#include "debug_tool/debug_tool_host.h"
#include "agent_management/agent_management.h"
#include "runtime_systems/runtime_systems.h"

#include <chrono>
#include <cassert>
#include <memory>
#include <utility>
#include <vector>
#include <string>

namespace debug_tool_backend {

using debug_tool::IDebugToolHost;

class DebugToolBackendRuntime : public IDebugToolHost {
 public:
  ~DebugToolBackendRuntime();

  bool Init(const char* window_title, int width, int height);
  bool Tick();
  void Destroy();

  bool has_agents() const override;
  size_t agent_count() const override;
  bool GetAgentSummary(size_t agent_index, debug_tool::AgentRuntimeSummary* out_summary) const override;
  const AlgorithmToAgentSignal& combined_algorithm_to_agent_signal() const override;
  bool IsPipelineAlgorithm(
    const std::string& algorithm_name,
    bool* out_is_pipeline,
    std::string* out_error_message = nullptr) const override;
  bool AttachAlgorithmToAgent(
    size_t agent_index,
    const std::string& algorithm_name,
    const std::vector<debug_tool::AlgorithmResourceBinding>& resource_bindings,
    const std::vector<debug_tool::AlgorithmDescriptorValue>& descriptor_values,
    size_t* out_algorithm_index = nullptr,
    std::string* out_error_message = nullptr,
    debug_tool::AlgorithmMountMode mount_mode = debug_tool::AlgorithmMountMode::Direct,
    debug_tool::AlgorithmExecutionPreference execution_preference = debug_tool::AlgorithmExecutionPreference::Gpu) override;
  bool AttachPipelineAlgorithmToAgent(
    size_t agent_index,
    const std::string& pipeline_name,
    const std::vector<debug_tool::AlgorithmPipelineStageSubmission>& stage_submissions,
    size_t* out_algorithm_index = nullptr,
    std::string* out_error_message = nullptr,
    debug_tool::AlgorithmExecutionPreference execution_preference = debug_tool::AlgorithmExecutionPreference::Gpu) override;
  bool AttachPipelinePackageToAgent(
    size_t agent_index,
    const std::string& pipeline_name,
    const std::string& pipeline_algorithm_name,
    const std::vector<debug_tool::AlgorithmResourceBinding>& resource_bindings,
    const std::vector<debug_tool::AlgorithmDescriptorValue>& descriptor_values,
    size_t* out_algorithm_index = nullptr,
    std::string* out_error_message = nullptr,
    debug_tool::AlgorithmExecutionPreference execution_preference = debug_tool::AlgorithmExecutionPreference::Gpu) override;
  bool DetachAlgorithmFromAgent(
    size_t agent_index,
    size_t algorithm_index,
    std::string* out_error_message = nullptr) override {
    return agent_manager_.DetachAlgorithmFromAgent(agent_index, algorithm_index, out_error_message);
  }
  bool ReplayPipelineStageBridgeDebug(
    size_t agent_index,
    size_t algorithm_index,
    std::string* out_error_message = nullptr) override;
  bool HotReloadAlgorithmPackage(
    size_t agent_index,
    size_t algorithm_index,
    size_t* out_algorithm_index = nullptr,
    std::string* out_error_message = nullptr) override;
  void StartTicking() override {
    agent_manager_.StartTicking();
    (void)agent_manager_.Tick(
      runtime_environment_.input(),
      runtime_environment_.MousePosition(),
      frame_dt_,
      render_preview_extent_);
  }
  void PauseTicking() override {
    agent_manager_.PauseTicking();
  }
  bool tick_enabled() const override {
    return agent_manager_.tick_enabled();
  }
  bool TickManagedAgents() override {
    return agent_manager_.Tick(
      runtime_environment_.input(),
      runtime_environment_.MousePosition(),
      frame_dt_,
      render_preview_extent_);
  }
  void ClearAgents() override {
    agent_manager_.ClearAgents();
  }
  void ClearGpuRuntimeCaches() override {
    runtime_environment_.ClearGpuRuntimeCaches();
  }
  bool LoadAlgorithmCatalog(
    std::vector<debug_tool::AlgorithmCatalogEntry>* out_entries,
    std::string* out_error_message = nullptr) const override;
  bool QueryAlgorithmRequestedBindings(
    const std::string& algorithm_name,
    std::vector<debug_tool::RequestedResourceEntry>* out_resources,
    std::vector<debug_tool::RequestedDescriptorEntry>* out_descriptors,
    std::string* out_error_message = nullptr) const override;
  bool LoadAlgorithmPackageDefaultBindings(
    const std::string& algorithm_name,
    std::vector<debug_tool::AlgorithmResourceBinding>* out_resource_bindings,
    std::vector<debug_tool::AlgorithmDescriptorValue>* out_descriptor_values,
    bool* out_has_default_file = nullptr,
    std::string* out_error_message = nullptr) const override;
  bool BuildRenderPreviewRequest(
    size_t agent_index,
    size_t algorithm_index,
    runtime_systems::RenderPreviewRequest* out_request,
    std::string* out_error_message = nullptr) const override;
  const InputState& input() const override { return runtime_environment_.input(); }
  Vec2 mouse_position() const override { return runtime_environment_.MousePosition(); }
  float frame_dt_seconds() const override { return frame_dt_; }
  bool has_render_preview_texture() const override { return runtime_environment_.HasRenderPreviewTexture(); }
  std::string render_preview_debug_summary() const override { return runtime_environment_.RenderPreviewDebugSummary(); }
  ImTextureID render_preview_texture_id() const override { return runtime_environment_.RenderPreviewTextureId(); }
  ImVec2 render_preview_texture_size() const override { return runtime_environment_.RenderPreviewTextureSize(); }
  void SetRenderPreviewExtent(ImVec2 extent) override {
    render_preview_extent_ = Vec2{extent.x, extent.y};
    runtime_environment_.SetRenderPreviewExtent(extent);
  }
  void SetRenderPreviewRequest(runtime_systems::RenderPreviewRequest request) override {
    if (request.valid) {
      assert(!request.stage_name.empty() && "Render preview request is missing a stage name.");
      assert(!request.storage_buffers.empty() && "Render preview request is missing storage buffers.");
    }
    render_preview_request_ = std::move(request);
    runtime_environment_.SetRenderPreviewRequest(render_preview_request_);
  }
  std::string& ui_status_message() override { return ui_status_message_; }
  const std::string& ui_status_message() const override { return ui_status_message_; }
  runtime_systems::RuntimeEnvironment& runtime_environment() { return runtime_environment_; }
  const runtime_systems::RuntimeEnvironment& runtime_environment() const { return runtime_environment_; }

 private:
  bool CreateAgent(const char* agent_name, uint32_t limit_fps_flag, size_t* out_agent_index = nullptr);
  runtime_systems::RuntimeEnvironment runtime_environment_{}; 
  AgentManager agent_manager_{};
  runtime_systems::RenderPreviewRequest render_preview_request_{};
  std::string ui_status_message_{};
  std::chrono::steady_clock::time_point last_frame_time_{};
  float frame_dt_{0.0f};
  Vec2 render_preview_extent_{1024.0f, 1024.0f};
};

}  // namespace debug_tool_backend

using debug_tool_backend::DebugToolBackendRuntime;
