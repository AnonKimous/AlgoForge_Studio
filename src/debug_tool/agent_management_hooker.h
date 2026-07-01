#pragma once

#include "agent_management/agent_management.h"
#include "common_data/input_state.h"
#include "common_data/vector_types.h"

#include <memory>
#include <string>
#include <vector>
#include <utility>

namespace debug_tool_backend::agent_management_hooker {

class AgentManagementHooker {
 public:
  bool CreateAgent(agent_management::AgentCreateSpec spec, size_t* out_agent_index = nullptr) {
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
    agent::AlgorithmExecutionPreference execution_preference = agent::AlgorithmExecutionPreference::Gpu) {
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

  bool AttachPipelineAlgorithmToAgent(
    size_t agent_index,
    const std::string& pipeline_name,
    const std::vector<agent::AlgorithmPipelineStageSubmission>& stage_submissions,
    size_t* out_algorithm_index = nullptr,
    std::string* out_error_message = nullptr,
    agent::AlgorithmExecutionPreference execution_preference = agent::AlgorithmExecutionPreference::Gpu,
    agent::AlgorithmPipelineTopology topology = agent::AlgorithmPipelineTopology::NonCircular,
    agent::AlgorithmPipelineSyncMode sync_mode = agent::AlgorithmPipelineSyncMode::Forced) {
    return agent_manager_.AttachPipelineAlgorithmToAgent(
      agent_index,
      pipeline_name,
      stage_submissions,
      out_algorithm_index,
      out_error_message,
      execution_preference,
      topology,
      sync_mode);
  }

  bool EnqueuePipelineStage0Submission(
    size_t agent_index,
    const std::string& pipeline_name,
    const std::vector<agent::AlgorithmResourceBinding>& resource_bindings,
    const std::vector<agent::AlgorithmDescriptorValue>& descriptor_values,
    std::string* out_error_message = nullptr) {
    return agent_manager_.EnqueuePipelineStage0Submission(
      agent_index,
      pipeline_name,
      resource_bindings,
      descriptor_values,
      out_error_message);
  }

  bool DetachAlgorithmFromAgent(
    size_t agent_index,
    size_t algorithm_index,
    std::string* out_error_message = nullptr) {
    return agent_manager_.DetachAlgorithmFromAgent(agent_index, algorithm_index, out_error_message);
  }

  bool ReplayPipelineStageBridgeDebug(
    size_t agent_index,
    size_t algorithm_index,
    const agent::AgentTickContext& context,
    std::string* out_error_message = nullptr) {
    return agent_manager_.ReplayPipelineStageBridgeDebug(
      agent_index,
      algorithm_index,
      context,
      out_error_message);
  }

  void Destroy() {
    agent_manager_.Destroy();
  }

  void ClearAgents() {
    agent_manager_.ClearAgents();
  }

  void StartTicking() {
    agent_manager_.StartTicking();
  }

  void PauseTicking() {
    agent_manager_.PauseTicking();
  }

  bool tick_enabled() const {
    return agent_manager_.tick_enabled();
  }

  bool Tick(
    const common_data::InputState& input,
    common_data::Vec2 mouse_pixel,
    float dt_seconds,
    common_data::Vec2 render_preview_extent = common_data::Vec2{1024.0f, 1024.0f}) {
    return agent_manager_.Tick(input, mouse_pixel, dt_seconds, render_preview_extent);
  }

  size_t agent_count() const {
    return agent_manager_.agent_count();
  }

  bool has_agents() const {
    return agent_manager_.has_agents();
  }

  std::shared_ptr<agent::Agent> agent(size_t index) const {
    return agent_manager_.agent(index);
  }

  const common_data::AlgorithmToAgentSignal& combined_algorithm_to_agent_signal() const {
    return agent_manager_.combined_algorithm_to_agent_signal();
  }

  agent_management::AgentManager& manager() {
    return agent_manager_;
  }

  const agent_management::AgentManager& manager() const {
    return agent_manager_;
  }

 private:
  agent_management::AgentManager agent_manager_{};
};

}  // namespace debug_tool_backend::agent_management_hooker
