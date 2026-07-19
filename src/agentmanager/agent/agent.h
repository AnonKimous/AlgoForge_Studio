#pragma once

#include "algomanager/algorithm_manager.h"
#include "common_data/common_data.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace agentmanager {
namespace agent {
using algomanager::bridge::AgentAlgorithmRuntimeState;
using algomanager::bridge::AgentInitConfig;
using algomanager::bridge::AgentTickContext;
using algomanager::bridge::AgentTickResult;
using algorithm::AlgorithmContainer;
using algorithm::AlgorithmContainerSet;
using algomanager::bridge::AlgorithmAssemblySlot;
using algomanager::bridge::AlgorithmAssemblyState;
using algomanager::bridge::AlgorithmDescriptorValue;
using algomanager::bridge::AlgorithmExecutionPreference;
using algomanager::bridge::AlgorithmPhaseKind;
using algomanager::bridge::AlgorithmPhaseSpec;
using algomanager::bridge::AlgorithmMountMode;
using algomanager::bridge::AlgorithmObject;
using algomanager::bridge::AlgorithmPipelineStageSubmission;
using algomanager::bridge::AlgorithmPipelineStageRuntimeStat;
using algomanager::bridge::AlgorithmPipelineSyncMode;
using algomanager::bridge::AlgorithmPipelineTopology;
using algomanager::bridge::AlgorithmPackageDebugState;
using algomanager::bridge::AlgorithmPhaseContainerBinding;
using algorithm::AlgorithmProfile;
using algomanager::bridge::AlgorithmRequestedDescriptorBindings;
using algomanager::bridge::AlgorithmRequestedResources;
using algomanager::bridge::AlgorithmResourceBinding;
using algomanager::bridge::AlgorithmReflectionSnapshot;
using algomanager::bridge::AlgorithmReflectionValue;
using algomanager::bridge::AlgorithmTickLifetime;
using algorithm::FindAlgorithmContainer;
using algomanager::bridge::IComplexAlgorithmPackageSupport;
using algomanager::bridge::PipelineStageBridgeDebugBinding;
using algomanager::bridge::PipelineStageBridgeDebugSet;

class Agent;

struct AlgorithmReadableReflection {
  std::string algorithm_name;
  std::vector<std::pair<std::string, std::string>> fields;
  bool valid{false};
};

struct AlgorithmProfileReflection {
  std::string algorithm_name;
  AlgorithmProfile profile{};
  bool valid{false};
};

bool CreateAlgorithmObjectByName(
  const std::string& algorithm_name,
  AlgorithmObject* out_group,
  std::string* out_error_message = nullptr,
  bool load_reflector = true);
bool QueryAlgorithmRequestedBindingsByName(
  const std::string& algorithm_name,
  AlgorithmRequestedResources* out_requested_resources,
  AlgorithmRequestedDescriptorBindings* out_requested_descriptor_bindings,
  std::string* out_error_message = nullptr);

class Agent {
 public:
  bool Init(AgentInitConfig config);
  bool MountAlgorithm(
    const std::string& algorithm_name,
    const std::vector<AlgorithmResourceBinding>& resource_bindings,
    const std::vector<AlgorithmDescriptorValue>& descriptor_values,
    size_t* out_index = nullptr,
    std::string* out_error_message = nullptr,
    AlgorithmMountMode mount_mode = AlgorithmMountMode::Direct,
     AlgorithmExecutionPreference execution_preference = AlgorithmExecutionPreference::Vk,
     bool load_reflector = true);
  bool MountPipelineAlgorithm(
    const std::string& pipeline_name,
    const std::vector<AlgorithmPipelineStageSubmission>& stage_submissions,
    size_t* out_index = nullptr,
    std::string* out_error_message = nullptr,
     AlgorithmExecutionPreference execution_preference = AlgorithmExecutionPreference::Vk,
    AlgorithmPipelineTopology topology = AlgorithmPipelineTopology::NonCircular,
    AlgorithmPipelineSyncMode sync_mode = AlgorithmPipelineSyncMode::Forced,
    bool load_reflector = true);
  bool RemoveAlgorithm(size_t index);
  void RefreshInterventionSignals(const AgentTickContext& context);
  bool Tick(
    const AgentTickContext& context,
    const std::vector<bool>& allow_tick_mask,
    AgentTickResult* out_result);
  bool ReplayPipelineStageBridgeDebug(
    size_t index,
    const AgentTickContext& context,
    std::string* out_error_message = nullptr);
  bool EnqueuePipelineStage0Submission(
    const std::string& pipeline_name,
    const std::vector<AlgorithmResourceBinding>& resource_bindings,
    const std::vector<AlgorithmDescriptorValue>& descriptor_values,
    std::string* out_error_message = nullptr,
    bool load_reflector = true);
  void RequestTimingLog();
  void Destroy();

  bool initialized() const { return initialized_; }
  const std::string& agent_name() const { return agent_name_; }
  size_t algorithm_count() const { return algorithm_objects_.size(); }
  const std::vector<AlgorithmObject>& algorithm_objects() const { return algorithm_objects_; }
  AlgorithmObject* algorithm_object(size_t index) {
    return index < algorithm_objects_.size() ? &algorithm_objects_[index] : nullptr;
  }
  const AlgorithmObject* algorithm_object(size_t index) const {
    return index < algorithm_objects_.size() ? &algorithm_objects_[index] : nullptr;
  }
  AlgorithmAssemblyState algorithm_assembly_state(size_t index) const {
    return index < algorithm_assembly_states_.size() ? algorithm_assembly_states_[index] : AlgorithmAssemblyState::Failed;
  }
  bool BeginAlgorithmAssembly(size_t index);
  void MarkAlgorithmAssemblyReady(size_t index);
  void MarkAlgorithmAssemblyFailed(size_t index);
  bool GetAlgorithmAssemblySlot(size_t index, AlgorithmAssemblySlot* out_slot);
  bool CollectAlgorithmReflection(size_t index, AlgorithmReflectionSnapshot* out_snapshot) const;
  const std::vector<AgentAlgorithmRuntimeState>& algorithm_runtime_states() const { return algorithm_runtime_states_; }
  AgentAlgorithmRuntimeState* algorithm_runtime_state(size_t index) {
    return index < algorithm_runtime_states_.size() ? &algorithm_runtime_states_[index] : nullptr;
  }
  const AgentAlgorithmRuntimeState* algorithm_runtime_state(size_t index) const {
    return index < algorithm_runtime_states_.size() ? &algorithm_runtime_states_[index] : nullptr;
  }
  bool PipelineNameInUse(const std::string& pipeline_name) const;

 private:
  bool AppendAlgorithmObject(AlgorithmObject object, size_t* out_index = nullptr);
  bool SubmitAlgorithm(
    const AgentTickContext& context,
    const std::vector<bool>& allow_tick_mask,
    AgentTickResult* out_result);
  bool initialized_{false};
  std::string agent_name_{}; 
  std::vector<AlgorithmObject> algorithm_objects_{};
  std::vector<AgentAlgorithmRuntimeState> algorithm_runtime_states_{};
  std::vector<AlgorithmAssemblyState> algorithm_assembly_states_{};
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>> standard_shared_container_sets_{};
  bool timing_log_requested_{false};
};

}  // namespace agent
}  // namespace agentmanager
