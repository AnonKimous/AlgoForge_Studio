#pragma once

#if !defined(AGENT_MANAGEMENT_LAYER_INTERNAL_BUILD) && !defined(AGENT_MANAGEMENT_LAYER_PUBLIC_FACADE_INCLUDE)
#error "Do not include agent_management/agent_manager.h directly. Use agent_management/agent_management.h."
#endif

#include "agent/agent.h"
#include "common_data/common_data.h"

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace agent_management {

struct AgentCreateSpec {
  std::string agent_name;
  // 0 means "tick once and then hold"; otherwise this is the maximum tick rate in Hz.
  uint32_t limit_fps_flag{120u};
  struct AlgorithmMountSpec {
    std::string algorithm_name;
    std::vector<agent::AlgorithmResourceBinding> resource_bindings;
    std::vector<agent::AlgorithmDescriptorValue> descriptor_values;
    agent::AlgorithmMountMode mount_mode{agent::AlgorithmMountMode::Direct};
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
  std::vector<algorithm_management::AlgorithmPipelineStageRuntimeStat> stage_runtime_stats;
};

bool ReportAlgorithmPipelineStall(
  const AlgorithmPipelineStallReport& report,
  std::string* out_error_message = nullptr);

class AgentManager {
 public:
  AgentManager();
  ~AgentManager();

  bool CreateAgent(AgentCreateSpec spec, size_t* out_agent_index = nullptr);
  bool AttachAlgorithmToAgent(
    size_t agent_index,
    const std::string& algorithm_name,
    const std::vector<agent::AlgorithmResourceBinding>& resource_bindings,
    const std::vector<agent::AlgorithmDescriptorValue>& descriptor_values,
    size_t* out_algorithm_index = nullptr,
    std::string* out_error_message = nullptr,
    agent::AlgorithmMountMode mount_mode = agent::AlgorithmMountMode::Direct,
    agent::AlgorithmExecutionPreference execution_preference = agent::AlgorithmExecutionPreference::Gpu);
  bool AttachPipelineAlgorithmToAgent(
    size_t agent_index,
    const std::string& pipeline_name,
    const std::vector<agent::AlgorithmPipelineStageSubmission>& stage_submissions,
    size_t* out_algorithm_index = nullptr,
    std::string* out_error_message = nullptr,
    agent::AlgorithmExecutionPreference execution_preference = agent::AlgorithmExecutionPreference::Gpu,
    agent::AlgorithmPipelineSubmissionMode submission_mode = agent::AlgorithmPipelineSubmissionMode::NonCircular);
  bool EnqueuePipelineStage0Submission(
    size_t agent_index,
    const std::string& pipeline_name,
    const std::vector<agent::AlgorithmResourceBinding>& resource_bindings,
    const std::vector<agent::AlgorithmDescriptorValue>& descriptor_values,
    std::string* out_error_message = nullptr);
  bool DetachAlgorithmFromAgent(
    size_t agent_index,
    size_t algorithm_index,
    std::string* out_error_message = nullptr);
  bool ReplayPipelineStageBridgeDebug(
    size_t agent_index,
    size_t algorithm_index,
    const agent::AgentTickContext& context,
    std::string* out_error_message = nullptr);
  bool DestroyAgent(size_t agent_index);
  void ClearAgents();
  void StartTicking();
  void PauseTicking();
  bool tick_enabled() const { return tick_enabled_; }
  bool Tick(const InputState& input, Vec2 mouse_pixel, float dt_seconds);
  bool CollectAlgorithmReflection(
    size_t agent_index,
    size_t algorithm_index,
    AlgorithmReflectionSnapshot* out_snapshot) const;
  void Destroy();

  size_t agent_count() const;
  bool has_agents() const;
  std::shared_ptr<agent::Agent> agent(size_t index) const;
  const AlgorithmToAgentSignal& combined_algorithm_to_agent_signal() const {
    return combined_algorithm_to_agent_signal_;
  }

 private:
  struct ManagedAgentEntry;

  std::vector<std::shared_ptr<ManagedAgentEntry>> managed_agents_{};
  AlgorithmToAgentSignal combined_algorithm_to_agent_signal_{};
  bool tick_enabled_{false};
};

}  // namespace agent_management

using agent_management::AgentCreateSpec;
using agent_management::AgentManager;
