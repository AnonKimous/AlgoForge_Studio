#pragma once

#define AGENT_MANAGEMENT_LAYER_INTERNAL_BUILD 1
#include "agentmanager/agent_management.h"
#undef AGENT_MANAGEMENT_LAYER_INTERNAL_BUILD
#include "common_data/input_state.h"
#include "common_data/vector_types.h"

#include <memory>
#include <string>
#include <vector>
#include <utility>

namespace debug_tool_backend::agent_management_hooker {

class AgentManagementHooker {
 public:
  bool CreateAgent(agentmanager::AgentCreateSpec spec, size_t* out_agent_index = nullptr) {
    return agent_manager_.CreateAgent(std::move(spec), out_agent_index);
  }

  bool AttachAlgorithmToAgent(
    size_t agent_index,
    const std::string& algorithm_name,
    const std::vector<agentmanager::agent::AlgorithmResourceBinding>& resource_bindings,
    const std::vector<agentmanager::agent::AlgorithmDescriptorValue>& descriptor_values,
    size_t* out_algorithm_index = nullptr,
    std::string* out_error_message = nullptr,
    agentmanager::agent::AlgorithmMountMode mount_mode = agentmanager::agent::AlgorithmMountMode::Direct,
    agentmanager::agent::AlgorithmExecutionPreference execution_preference = agentmanager::agent::AlgorithmExecutionPreference::Vk,
    bool load_reflector = true) {
    return agent_manager_.AttachAlgorithmToAgent(
      agent_index,
      algorithm_name,
      resource_bindings,
      descriptor_values,
      out_algorithm_index,
      out_error_message,
      mount_mode,
      execution_preference,
      load_reflector);
  }

  bool AttachPipelineAlgorithmToAgent(
    size_t agent_index,
    const std::string& pipeline_name,
    const std::vector<agentmanager::agent::AlgorithmPipelineStageSubmission>& stage_submissions,
    size_t* out_algorithm_index = nullptr,
    std::string* out_error_message = nullptr,
    agentmanager::agent::AlgorithmExecutionPreference execution_preference = agentmanager::agent::AlgorithmExecutionPreference::Vk,
    agentmanager::agent::AlgorithmPipelineTopology topology = agentmanager::agent::AlgorithmPipelineTopology::NonCircular,
    agentmanager::agent::AlgorithmPipelineSyncMode sync_mode = agentmanager::agent::AlgorithmPipelineSyncMode::Forced,
    bool load_reflector = true) {
    return agent_manager_.AttachPipelineAlgorithmToAgent(
      agent_index,
      pipeline_name,
      stage_submissions,
      out_algorithm_index,
      out_error_message,
      execution_preference,
      topology,
      sync_mode,
      load_reflector);
  }

  bool EnqueuePipelineStage0Submission(
    size_t agent_index,
    const std::string& pipeline_name,
    const std::vector<agentmanager::agent::AlgorithmResourceBinding>& resource_bindings,
    const std::vector<agentmanager::agent::AlgorithmDescriptorValue>& descriptor_values,
    std::string* out_error_message = nullptr) {
    const bool load_reflector = false;
    return agent_manager_.EnqueuePipelineStage0Submission(
      agent_index,
      pipeline_name,
      resource_bindings,
      descriptor_values,
      out_error_message,
      load_reflector);
  }

  bool RequestAgentTimingLog(
    size_t agent_index,
    std::string* out_error_message = nullptr) {
    return agent_manager_.RequestAgentTimingLog(agent_index, out_error_message);
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
    const agentmanager::agent::AgentTickContext& context,
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

  std::shared_ptr<agentmanager::agent::Agent> agent(size_t index) const {
    return agent_manager_.agent(index);
  }

  const common_data::AlgorithmToAgentSignal& combined_algorithm_to_agent_signal() const {
    return agent_manager_.combined_algorithm_to_agent_signal();
  }

  agentmanager::AgentManager& manager() {
    return agent_manager_;
  }

  const agentmanager::AgentManager& manager() const {
    return agent_manager_;
  }

 private:
  agentmanager::AgentManager agent_manager_{};
};

}  // namespace debug_tool_backend::agent_management_hooker

