#pragma once

#include "algorithm_management/algorithm_manager.h"
#include "agent/agent_abi.h"
#include "common_data/common_data.h"

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace agent {

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
    AlgorithmExecutionPreference execution_preference = AlgorithmExecutionPreference::Gpu);
  bool MountPipelineAlgorithm(
    const std::string& pipeline_name,
    const std::vector<AlgorithmPipelineStageSubmission>& stage_submissions,
    size_t* out_index = nullptr,
    std::string* out_error_message = nullptr,
    AlgorithmExecutionPreference execution_preference = AlgorithmExecutionPreference::Gpu);
  bool AppendAlgorithmObject(AlgorithmObject object, size_t* out_index = nullptr);
  bool RemoveAlgorithm(size_t index);
  void RefreshInterventionSignals(const AgentTickContext& context);
  bool SubmitAlgorithm(
    const AgentTickContext& context,
    const std::vector<bool>& allow_tick_mask,
    AgentTickResult* out_result);
  bool Tick(
    const AgentTickContext& context,
    const std::vector<bool>& allow_tick_mask,
    AgentTickResult* out_result);
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
  const AlgorithmObject* FindAlgorithmObject(const std::string& algorithm_name) const {
    for (const AlgorithmObject& object : algorithm_objects_) {
      if (object.algorithm_profile.algorithm_name == algorithm_name) {
        return &object;
      }
    }
    return nullptr;
  }
  const std::vector<AgentAlgorithmRuntimeState>& algorithm_runtime_states() const { return algorithm_runtime_states_; }
  AgentAlgorithmRuntimeState* algorithm_runtime_state(size_t index) {
    return index < algorithm_runtime_states_.size() ? &algorithm_runtime_states_[index] : nullptr;
  }
  const AgentAlgorithmRuntimeState* algorithm_runtime_state(size_t index) const {
    return index < algorithm_runtime_states_.size() ? &algorithm_runtime_states_[index] : nullptr;
  }
  bool PipelineNameInUse(const std::string& pipeline_name) const;

 private:
  bool initialized_{false};
  std::string agent_name_{}; 
  std::vector<AlgorithmObject> algorithm_objects_{};
  std::vector<AgentAlgorithmRuntimeState> algorithm_runtime_states_{};
  std::vector<AlgorithmAssemblyState> algorithm_assembly_states_{};
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>> standard_shared_container_sets_{};
};

}  // namespace agent
