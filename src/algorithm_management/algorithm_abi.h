#pragma once

#include "algorithm_support/algorithm_data.h"
#include "algorithm_support/algorithm_types.h"
#include "common_data/common_data.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace algorithm_management {

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

enum class AlgorithmMountMode {
  Direct = 0,
  StandardContainer = 1,
};

enum class AlgorithmExecutionPreference {
  Cpu = 0,
  Gpu = 1,
};

enum class AlgorithmExecutionPhase {
  Body = 0,
  PreExecution = 1,
  PostExecution = 2,
  ResultRender = 3,
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
  std::vector<AdvancedAlgorithmDebugSignal> signals;
};

struct AlgorithmInterventionPackageDebugState {
  std::vector<AdvancedAlgorithmDebugSignal> signals;
  AlgorithmToAgentSignal algorithm_to_agent_signal{};
};

struct AgentTickContext {
  const InputState* input{nullptr};
  Vec2 mouse_pixel{};
  float dt_seconds{0.0f};
  AlgorithmExecutionPhase execution_phase{AlgorithmExecutionPhase::Body};
  const InteractionInterventionRequest* intervention_request{nullptr};
};

struct AgentAlgorithmRuntimeState {
  std::string algorithm_name;
  AgentToAlgorithmSignal agent_to_algorithm_signal{};
  AlgorithmToAgentSignal algorithm_to_agent_signal{};
  AlgorithmPackageDebugState debug_state{};
  AlgorithmReflectionSnapshot reflection_snapshot{};
};

enum class AlgorithmAssemblyState {
  Pending,
  Assembling,
  Ready,
  Failed,
};

struct AgentTickResult {
  AlgorithmToAgentSignal algorithm_to_agent_signal{};
  std::vector<AgentAlgorithmRuntimeState> algorithm_runtime_states;
};

class IAlgorithmPackageSupport;
class IAlgorithmtemporaryTestMainThreadExecutor;
class IAlgorithmIntervention;
class AlgorithmObject;

struct AlgorithmAssemblySlot {
  size_t index{0u};
  AlgorithmObject* algorithm_object{nullptr};
  AlgorithmAssemblyState* assembly_state{nullptr};
};

class AlgorithmObject {
 public:
  AlgorithmObject() : shared_container_set(std::make_shared<algorithm::AlgorithmContainerSet>()) {}

  algorithm::AlgorithmContainerSet* mutable_container_set() {
    EnsureContainerSet();
    return shared_container_set.get();
  }
  const algorithm::AlgorithmContainerSet* container_set() const {
    return shared_container_set.get();
  }
  void SetContainerSet(std::shared_ptr<algorithm::AlgorithmContainerSet> container_set) {
    shared_container_set = std::move(container_set);
    EnsureContainerSet();
  }

  algorithm::AlgorithmProfile algorithm_profile{};
  std::shared_ptr<IAlgorithmPackageSupport> reflector;
  std::shared_ptr<algorithm::AlgorithmReflector> algorithm_reflector;
  std::shared_ptr<algorithm::AlgorithmContainerSet> shared_container_set;
  std::vector<AlgorithmResourceBinding> resource_bindings;
  std::vector<AlgorithmDescriptorValue> descriptor_values;
  bool cpu_symbol{true};
  bool gpu_symbol{true};
  AlgorithmMountMode mount_mode{AlgorithmMountMode::Direct};
  AlgorithmExecutionPreference execution_preference{AlgorithmExecutionPreference::Gpu};
  std::shared_ptr<IAlgorithmtemporaryTestMainThreadExecutor> temporaryTest_main_thread_executor;
  std::shared_ptr<IAlgorithmIntervention> intervention;

 private:
  void EnsureContainerSet() {
    if (!shared_container_set) {
      shared_container_set = std::make_shared<algorithm::AlgorithmContainerSet>();
    }
  }
};

struct AgentInitConfig {
  std::string agent_name;
  std::vector<AlgorithmObject> algorithm_objects;
};

class IAlgorithmPackageSupport {
 public:
  virtual ~IAlgorithmPackageSupport() = default;

  virtual bool BuildAlgorithmProfile(
    const VolumeDescriptor& volume,
    algorithm::AlgorithmProfile* out_profile) const = 0;

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

class ISimpleAlgorithmPackageSupport : public IAlgorithmPackageSupport {
 public:
  ~ISimpleAlgorithmPackageSupport() override = default;
};

class IComplexAlgorithmPackageSupport : public IAlgorithmPackageSupport {
 public:
  ~IComplexAlgorithmPackageSupport() override = default;
  virtual void CollectDebugState(AlgorithmPackageDebugState* debug_state) const = 0;
};

class IAlgorithmtemporaryTestMainThreadExecutor {
 public:
  virtual ~IAlgorithmtemporaryTestMainThreadExecutor() = default;

  virtual bool temporaryTestExecuteOnMainThread(
    const AgentTickContext& context,
    const algorithm::AlgorithmProfile& algorithm_profile,
    const AgentToAlgorithmSignal& agent_to_algorithm_signal,
    algorithm::AlgorithmContainerSet* algorithm_container_set,
    AlgorithmToAgentSignal* algorithm_to_agent_signal,
    AlgorithmPackageDebugState* debug_state) = 0;
};

enum class AlgorithmInterventionStageKind {
  ResultRender = 0,
  PreExecution = 1,
  InExecution = 2,
  PostExecution = 3,
  Custom = 4,
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
  std::vector<std::string> functions;
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

}  // namespace algorithm_management

namespace agent {
using algorithm_management::AgentAlgorithmRuntimeState;
using algorithm_management::AgentInitConfig;
using algorithm_management::AgentTickContext;
using algorithm_management::AgentTickResult;
using algorithm_management::AlgorithmAssemblySlot;
using algorithm_management::AlgorithmAssemblyState;
using algorithm_management::AlgorithmDescriptorValue;
using algorithm_management::AlgorithmExecutionPhase;
using algorithm_management::AlgorithmExecutionPreference;
using algorithm_management::AlgorithmInterventionContainerBinding;
using algorithm_management::AlgorithmInterventionPackageDebugState;
using algorithm_management::AlgorithmInterventionShaderSpec;
using algorithm_management::AlgorithmInterventionStageKind;
using algorithm_management::AlgorithmInterventionStageSpec;
using algorithm_management::AlgorithmMountMode;
using algorithm_management::AlgorithmObject;
using algorithm_management::AlgorithmPackageDebugState;
using algorithm_management::AlgorithmReflectionSnapshot;
using algorithm_management::AlgorithmReflectionValue;
using algorithm_management::AlgorithmRequestedDescriptorBindings;
using algorithm_management::AlgorithmRequestedResources;
using algorithm_management::AlgorithmResourceBinding;
using algorithm_management::IAlgorithmIntervention;
using algorithm_management::IAlgorithmPackageSupport;
using algorithm_management::IAlgorithmtemporaryTestMainThreadExecutor;
using algorithm_management::IComplexAlgorithmPackageSupport;
using algorithm_management::ISimpleAlgorithmPackageSupport;
}  // namespace agent
