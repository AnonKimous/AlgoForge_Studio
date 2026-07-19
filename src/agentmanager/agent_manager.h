#pragma once

#if !defined(AGENT_MANAGEMENT_LAYER_INTERNAL_BUILD) && !defined(AGENT_MANAGEMENT_LAYER_PUBLIC_FACADE_INCLUDE)
#error "Do not include agentmanager/agent_manager.h directly. Use agentmanager/agent_management.h."
#endif

#include "agentmanager/agent/agent.h"
#include "common_data/common_data.h"
#include "common_data/kernel_cfg.h"

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace agentmanager {
using algomanager::bridge::AlgorithmAssemblyState;
using algorithm::AlgorithmContainerStorageKind;
using algomanager::bridge::AlgorithmDescriptorValue;
using algomanager::bridge::AlgorithmExecutionPreference;
using algomanager::bridge::AlgorithmInterventionContainerBinding;
using algomanager::bridge::AlgorithmInterventionStageKind;
using algomanager::bridge::AlgorithmInterventionStageSpec;
using algomanager::bridge::AlgorithmMountMode;
using algomanager::bridge::AlgorithmPipelineStageRuntimeStat;
using algomanager::bridge::AlgorithmPipelineStageSubmission;
using algomanager::bridge::AlgorithmPipelineSyncMode;
using algomanager::bridge::AlgorithmPipelineTopology;
using algomanager::bridge::AlgorithmRequestedDescriptorBindings;
using algomanager::bridge::AlgorithmRequestedResources;
using algomanager::bridge::AlgorithmResourceBinding;
using algomanager::bridge::AlgorithmTickLifetime;
using algomanager::bridge::JobsPipelineRegistration;
using algomanager::bridge::JobsPipelineRuntimeState;
using algomanager::bridge::AgentAlgorithmRuntimeState;
using algomanager::bridge::AgentInitConfig;
using algomanager::bridge::AgentTickContext;
using algomanager::bridge::AgentTickResult;

struct AgentCreateSpec {
  std::string agent_name;
  // 0 means "tick once and then hold"; otherwise this is the maximum tick rate in Hz.
  uint32_t limit_fps_flag{common_data::DefaultAgentLimitFpsFlag()};
  struct AlgorithmMountSpec {
    std::string algorithm_name;
    std::vector<agentmanager::agent::AlgorithmResourceBinding> resource_bindings;
    std::vector<agentmanager::agent::AlgorithmDescriptorValue> descriptor_values;
    agentmanager::agent::AlgorithmMountMode mount_mode{agentmanager::agent::AlgorithmMountMode::Direct};
  };
  std::vector<AlgorithmMountSpec> algorithm_mount_specs;
};

struct AlgorithmReflectionRecord {
  std::string reflection_object_name;
  std::string container_name;
  std::string filter_name;
  AlgorithmContainerStorageKind storage_kind{AlgorithmContainerStorageKind::Array};
  std::vector<std::byte> bytes;
};

struct AlgorithmReflectionSnapshot {
  size_t agent_index{0u};
  size_t algorithm_index{0u};
  std::string agent_name;
  std::string algorithm_name;
  std::vector<AlgorithmReflectionRecord> variables;
  std::vector<AlgorithmReflectionRecord> variable_arrays;
  bool valid{false};

  void Clear() {
    agent_index = 0u;
    algorithm_index = 0u;
    agent_name.clear();
    algorithm_name.clear();
    variables.clear();
    variable_arrays.clear();
    valid = false;
  }
};

struct AlgorithmPipelineStallReport {
  std::string algorithm_name;
  float stalled_seconds{0.0f};
  std::string reason;
  std::vector<algomanager::bridge::AlgorithmPipelineStageRuntimeStat> stage_runtime_stats;
};

bool ReportAlgorithmPipelineStall(
  const AlgorithmPipelineStallReport& report,
  std::string* out_error_message = nullptr);

bool ExportAlgorithmPipelineTimingArtifacts(
  const AlgorithmPipelineStallReport& report,
  std::string* out_csv_path = nullptr,
  std::string* out_mermaid_path = nullptr,
  std::string* out_error_message = nullptr);

class AgentManager {
 public:
  AgentManager();
  ~AgentManager();

  bool CreateAgent(AgentCreateSpec spec, size_t* out_agent_index = nullptr);
  bool AttachAlgorithmToAgent(
    size_t agent_index,
    const std::string& algorithm_name,
    const std::vector<agentmanager::agent::AlgorithmResourceBinding>& resource_bindings,
    const std::vector<agentmanager::agent::AlgorithmDescriptorValue>& descriptor_values,
    size_t* out_algorithm_index = nullptr,
    std::string* out_error_message = nullptr,
    agentmanager::agent::AlgorithmMountMode mount_mode = agentmanager::agent::AlgorithmMountMode::Direct,
     agentmanager::agent::AlgorithmExecutionPreference execution_preference = agentmanager::agent::AlgorithmExecutionPreference::Vk,
    bool load_reflector = true);
  bool AttachPipelineAlgorithmToAgent(
    size_t agent_index,
    const std::string& pipeline_name,
    const std::vector<agentmanager::agent::AlgorithmPipelineStageSubmission>& stage_submissions,
    size_t* out_algorithm_index = nullptr,
    std::string* out_error_message = nullptr,
     agentmanager::agent::AlgorithmExecutionPreference execution_preference = agentmanager::agent::AlgorithmExecutionPreference::Vk,
    agentmanager::agent::AlgorithmPipelineTopology topology = agentmanager::agent::AlgorithmPipelineTopology::NonCircular,
    agentmanager::agent::AlgorithmPipelineSyncMode sync_mode = agentmanager::agent::AlgorithmPipelineSyncMode::Forced,
    bool load_reflector = true);
  bool EnqueuePipelineStage0Submission(
    size_t agent_index,
    const std::string& pipeline_name,
    const std::vector<agentmanager::agent::AlgorithmResourceBinding>& resource_bindings,
    const std::vector<agentmanager::agent::AlgorithmDescriptorValue>& descriptor_values,
    std::string* out_error_message = nullptr,
    bool load_reflector = true);
  bool RequestAgentTimingLog(
    size_t agent_index,
    std::string* out_error_message = nullptr);
  bool DetachAlgorithmFromAgent(
    size_t agent_index,
    size_t algorithm_index,
    std::string* out_error_message = nullptr);
  bool ReplayPipelineStageBridgeDebug(
    size_t agent_index,
    size_t algorithm_index,
    const agentmanager::agent::AgentTickContext& context,
    std::string* out_error_message = nullptr);
  bool DestroyAgent(size_t agent_index);
  void ClearAgents();
  void StartTicking();
  void PauseTicking();
  bool tick_enabled() const { return tick_enabled_; }
  bool Tick(
    const InputState& input,
    Vec2 mouse_pixel,
    float dt_seconds,
    Vec2 render_preview_extent = Vec2{1024.0f, 1024.0f});
  bool CollectAlgorithmReflection(
    size_t agent_index,
    size_t algorithm_index,
    AlgorithmReflectionSnapshot* out_snapshot) const;
  void Destroy();

  size_t agent_count() const;
  bool has_agents() const;
  std::shared_ptr<agentmanager::agent::Agent> agent(size_t index) const;
  const AlgorithmToAgentSignal& combined_algorithm_to_agent_signal() const {
    return combined_algorithm_to_agent_signal_;
  }

 private:
  struct ManagedAgentEntry;

  std::vector<std::shared_ptr<ManagedAgentEntry>> managed_agents_{};
  AlgorithmToAgentSignal combined_algorithm_to_agent_signal_{};
  bool tick_enabled_{false};
};

}  // namespace agentmanager

using agentmanager::AgentCreateSpec;
using agentmanager::AgentManager;

