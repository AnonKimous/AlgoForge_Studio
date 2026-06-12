#pragma once

#include "algorithm_management/algorithm_manager.h"
#include "algorithm_support/algorithm_data.h"
#include "common_data/interaction/interaction_signals.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace agent {

struct AgentTickContext;

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

struct AlgorithmPackageDebugState {
  std::vector<algorithm_support::AdvancedAlgorithmDebugSignal> signals;
};

struct AlgorithmInterventionPackageDebugState {
  std::vector<algorithm_support::AdvancedAlgorithmDebugSignal> signals;
  AlgorithmToAgentSignal algorithm_to_agent_signal{};
};

class IAlgorithmPackageSupport {
 public:
  virtual ~IAlgorithmPackageSupport() = default;

  virtual bool BuildAlgorithmProfile(
    const algorithm_support::VolumeDescriptor& volume,
    algorithm::AlgorithmProfile* out_profile) const = 0;

  virtual bool BuildMeshCoderOutput(const Mesh& mesh, algorithm_support::MeshCoderOutput* out_output) const {
    (void)mesh;
    (void)out_output;
    return false;
  }

  virtual bool ReflectMeshCommon(const Mesh& mesh, algorithm_support::MeshCommonReflection* out_reflection) const {
    (void)mesh;
    (void)out_reflection;
    return false;
  }

  virtual bool BuildVolumeDescriptor(
    const Mesh& mesh,
    float mass,
    Vec3 driving_dir,
    algorithm_support::VolumeDescriptor* out_volume) const {
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
    const algorithm::AlgorithmProfile& algorithm_profile,
    AlgorithmRequestedResources* out_requested_resources) const {
    (void)algorithm_profile;
    (void)out_requested_resources;
    return false;
  }

  virtual bool GetRequestedDescriptorBindings(
    const algorithm::AlgorithmProfile& algorithm_profile,
    AlgorithmRequestedDescriptorBindings* out_requested_descriptor_bindings) const {
    (void)algorithm_profile;
    (void)out_requested_descriptor_bindings;
    return false;
  }

  virtual bool Decompose(
    const algorithm::AlgorithmProfile& algorithm_profile,
    const std::vector<AlgorithmResourceBinding>& resource_bindings,
    const std::vector<AlgorithmDescriptorValue>& descriptor_values,
    algorithm::AlgorithmContainerSet* container_set,
    std::string* out_error_message = nullptr) const {
    (void)algorithm_profile;
    (void)resource_bindings;
    (void)descriptor_values;
    (void)container_set;
    (void)out_error_message;
    return true;
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
  FillSignal = 1,
  ResourceRefill = 2,
  Runtime = 3,
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

struct AgentAlgorithmSupportGroup {
  algorithm::AlgorithmProfile algorithm_profile{};
  std::shared_ptr<IAlgorithmPackageSupport> reflector;
  std::shared_ptr<IAlgorithmPackageDecomposer> decomposer;
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
};

struct AgentInitConfig {
  std::string agent_name;
  std::vector<AgentAlgorithmSupportGroup> algorithm_support_groups;
};

}  // namespace agent
