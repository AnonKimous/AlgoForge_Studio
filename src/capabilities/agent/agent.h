#pragma once

#include "algorithm_management/algorithm_manager.h"
#include "codec/codec_manager.h"
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

struct AlgorithmRequestedResources {
  struct RequiredResource {
    std::string resource_name;
    std::string resource_kind;
    bool required{true};
  };

  std::string algorithm_name;
  std::vector<RequiredResource> required_resources;
  bool valid{false};
};

struct AlgorithmRequestedDescriptorBindings {
  struct DescriptorSlot {
    std::string descriptor_name;
    std::string container_name;
    uint32_t array_index{0u};
  };

  std::string algorithm_name;
  std::vector<DescriptorSlot> descriptor_slots;
  bool valid{false};
};

struct AlgorithmResourceBinding {
  std::string resource_name;
  std::string resource_kind;
  std::string source_path;
};

struct AlgorithmDescriptorValue {
  std::string descriptor_name;
  float scalar_value{0.0f};
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

struct AlgorithmPackageDebugState {
  std::vector<codec::AdvancedAlgorithmDebugSignal> signals;
};

struct AlgorithmInterventionPackageDebugState {
  std::vector<codec::AdvancedAlgorithmDebugSignal> signals;
  AlgorithmToAgentSignal algorithm_to_agent_signal{};
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

class IAlgorithmPackageCodec {
 public:
  virtual ~IAlgorithmPackageCodec() = default;

  virtual bool BuildAlgorithmProfile(
    const codec::VolumeDescriptor& volume,
    AlgorithmProfile* out_profile) const = 0;

  virtual bool BuildMeshCoderOutput(const Mesh& mesh, MeshCoderOutput* out_output) const {
    (void)mesh;
    (void)out_output;
    return false;
  }

  virtual bool ReflectMeshCommon(const Mesh& mesh, MeshCommonReflection* out_reflection) const {
    (void)mesh;
    (void)out_reflection;
    return false;
  }
  // The reflector may expose only a selective subset of high-value data.
  // It decides what is worth reflecting.

  virtual bool BuildVolumeDescriptor(
    const Mesh& mesh,
    float mass,
    Vec3 driving_dir,
    VolumeDescriptor* out_volume) const {
    (void)mesh;
    (void)mass;
    (void)driving_dir;
    (void)out_volume;
    return false;
  }
};

class IAlgorithmPackageDecomposer {
 public:
  virtual ~IAlgorithmPackageDecomposer() = default;

  virtual bool GetRequestedResources(
    const AlgorithmProfile& algorithm_profile,
    AlgorithmRequestedResources* out_requested_resources) const {
    (void)algorithm_profile;
    (void)out_requested_resources;
    return false;
  }

  virtual bool GetRequestedDescriptorBindings(
    const AlgorithmProfile& algorithm_profile,
    AlgorithmRequestedDescriptorBindings* out_requested_descriptor_bindings) const {
    (void)algorithm_profile;
    (void)out_requested_descriptor_bindings;
    return false;
  }

  virtual bool Decompose(
    const AlgorithmProfile& algorithm_profile,
    const std::vector<AlgorithmResourceBinding>& resource_bindings,
    const std::vector<AlgorithmDescriptorValue>& descriptor_values,
    AlgorithmContainerSet* container_set,
    std::string* out_error_message = nullptr) const {
    (void)algorithm_profile;
    (void)resource_bindings;
    (void)descriptor_values;
    (void)container_set;
    (void)out_error_message;
    return true;
  }
};

class ISimpleAlgorithmPackageCodec : public IAlgorithmPackageCodec {
 public:
  ~ISimpleAlgorithmPackageCodec() override = default;
};

class IComplexAlgorithmPackageCodec : public IAlgorithmPackageCodec {
 public:
  ~IComplexAlgorithmPackageCodec() override = default;
  virtual void CollectDebugState(AlgorithmPackageDebugState* debug_state) const = 0;
};

// Temporary main-thread execution bridge for bring-up tests.
// Keep every temporary surface explicitly marked with `temporaryTest`.
class IAlgorithmtemporaryTestMainThreadExecutor {
 public:
  virtual ~IAlgorithmtemporaryTestMainThreadExecutor() = default;

  virtual bool temporaryTestExecuteOnMainThread(
    const AgentTickContext& context,
    const AlgorithmProfile& algorithm_profile,
    const AgentToAlgorithmSignal& agent_to_algorithm_signal,
    AlgorithmContainerSet* algorithm_container_set,
    AlgorithmToAgentSignal* algorithm_to_agent_signal,
    AlgorithmPackageDebugState* debug_state) = 0;
};

enum class AlgorithmInterventionStageKind {
  ResultRender = 0,
  FillSignal = 1,
  ResourceRefill = 2,
  Custom = 3,
};

struct AlgorithmInterventionContainerBinding {
  std::string container_name;
  std::string container_kind;
  uint32_t tuple_width{0u};
  bool required{true};
};

struct AlgorithmInterventionShaderSpec {
  std::string vertex_shader_path;
  std::string fragment_shader_path;
  std::string pipeline_kind;
};

struct AlgorithmInterventionStageSpec {
  std::string stage_name;
  AlgorithmInterventionStageKind stage_kind{AlgorithmInterventionStageKind::Custom};
  std::vector<AlgorithmInterventionContainerBinding> used_algorithm_containers;
  AlgorithmInterventionShaderSpec shader;
};

class IAlgorithmIntervention {
 public:
  virtual ~IAlgorithmIntervention() = default;

  virtual bool SupportsIntervention() const = 0;
  virtual void FillAgentToAlgorithmSignal(
    const AgentTickContext& context,
    AgentToAlgorithmSignal* out_signal) const = 0;
  virtual bool GetInterventionStageSpecs(
    std::vector<AlgorithmInterventionStageSpec>* out_stage_specs) const = 0;
};

struct AgentAlgorithmCodecGroup {
  AlgorithmProfile algorithm_profile{};
  std::shared_ptr<IAlgorithmPackageCodec> reflector;
  std::shared_ptr<IAlgorithmPackageDecomposer> decomposer;
  std::shared_ptr<algorithm::AlgorithmReflector> algorithm_reflector;
  std::vector<AlgorithmResourceBinding> resource_bindings;
  std::vector<AlgorithmDescriptorValue> descriptor_values;
  bool cpu_symbol{true};
  bool gpu_symbol{true};
  std::shared_ptr<IAlgorithmtemporaryTestMainThreadExecutor> temporaryTest_main_thread_executor;
  std::shared_ptr<IAlgorithmIntervention> intervention;
};

struct AgentInitConfig {
  std::string agent_name;
  std::vector<AgentAlgorithmCodecGroup> algorithm_codec_groups;
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
