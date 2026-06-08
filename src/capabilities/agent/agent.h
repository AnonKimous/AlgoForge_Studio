#pragma once

#include "algorithm_management/algorithm_manager.h"
#include "agent_management/agent_abi.h"
#include "common_data/common_data.h"

#include <cstddef>
#include <memory>
#include <string>
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

struct AlgorithmReflectionValue {
  std::string reflection_object_name;
  std::string container_name;
  std::string filter_name;
  algorithm::AlgorithmContainerStorageKind storage_kind{algorithm::AlgorithmContainerStorageKind::Array};
  std::vector<std::byte> bytes;
};

struct AlgorithmReflectionSnapshot {
  std::string algorithm_name;
  std::vector<AlgorithmReflectionValue> variables;
  std::vector<AlgorithmReflectionValue> variable_arrays;
  bool valid{false};

  void Clear() {
    algorithm_name.clear();
    variables.clear();
    variable_arrays.clear();
    valid = false;
  }
};

struct AgentAlgorithmRuntimeState {
  std::string algorithm_name;
  AgentToAlgorithmSignal agent_to_algorithm_signal{};
  AlgorithmToAgentSignal algorithm_to_agent_signal{};
  AlgorithmPackageDebugState debug_state{};
  AlgorithmReflectionSnapshot reflection_snapshot{};
};

class AlgorithmObject {
 public:
  AlgorithmContainerSet* mutable_container_set() { return &container_set_; }
  const AlgorithmContainerSet* container_set() const { return &container_set_; }

 private:
  AlgorithmContainerSet container_set_{};
};

enum class AlgorithmAssemblyState {
  Pending,
  Assembling,
  Ready,
  Failed,
};

struct AgentTickContext {
  const InputState* input{nullptr};
  Vec2 mouse_pixel{};
  float dt_seconds{0.0f};
  const InteractionInterventionRequest* intervention_request{nullptr};
};

struct AgentTickResult {
  AlgorithmToAgentSignal algorithm_to_agent_signal{};
  std::vector<AgentAlgorithmRuntimeState> algorithm_runtime_states;
};

struct AlgorithmAssemblySlot {
  size_t index{0u};
  AgentAlgorithmCodecGroup* algorithm_codec_group{nullptr};
  AlgorithmObject* algorithm_object{nullptr};
  AlgorithmAssemblyState* assembly_state{nullptr};
};

class Agent {
 public:
  bool Init(AgentInitConfig config);
  bool AppendAlgorithmCodecGroup(AgentAlgorithmCodecGroup group, size_t* out_index = nullptr);
  void RefreshInterventionSignals(const AgentTickContext& context);
  bool Tick(
    const AgentTickContext& context,
    const std::vector<bool>& allow_tick_mask,
    AgentTickResult* out_result);
  void Destroy();

  bool initialized() const { return initialized_; }
  const std::string& agent_name() const { return agent_name_; }
  size_t algorithm_count() const { return algorithm_codec_groups_.size(); }
  const std::vector<AgentAlgorithmCodecGroup>& algorithm_codec_groups() const { return algorithm_codec_groups_; }
  AgentAlgorithmCodecGroup* algorithm_codec_group(size_t index) {
    return index < algorithm_codec_groups_.size() ? &algorithm_codec_groups_[index] : nullptr;
  }
  const AgentAlgorithmCodecGroup* algorithm_codec_group(size_t index) const {
    return index < algorithm_codec_groups_.size() ? &algorithm_codec_groups_[index] : nullptr;
  }
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
  const AgentAlgorithmCodecGroup* FindAlgorithmCodecGroup(const std::string& algorithm_name) const {
    for (const AgentAlgorithmCodecGroup& group : algorithm_codec_groups_) {
      if (group.algorithm_profile.algorithm_name == algorithm_name) {
        return &group;
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

 private:
  bool initialized_{false};
  std::string agent_name_{};
  std::vector<AgentAlgorithmCodecGroup> algorithm_codec_groups_{};
  std::vector<AgentAlgorithmRuntimeState> algorithm_runtime_states_{};
  std::vector<AlgorithmObject> algorithm_objects_{};
  std::vector<AlgorithmAssemblyState> algorithm_assembly_states_{};
};

inline bool agent_init(Agent* agent_instance, AgentInitConfig config) {
  if (!agent_instance) return false;
  return agent_instance->Init(std::move(config));
}

inline void agent_destroy(Agent* agent_instance) {
  if (agent_instance) {
    agent_instance->Destroy();
  }
}

}  // namespace agent
