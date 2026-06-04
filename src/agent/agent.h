#pragma once

#include "algorithm/algorithm_types.h"
#include "codec/codec_manager.h"
#include "common_data/input_state.h"
#include "common_data/interaction/interaction_signals.h"
#include "common_data/mesh.h"

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

struct AlgorithmDescriptorShapeReflection {
  std::string algorithm_name;
  AlgorithmContainerDescriptor descriptor_shape{};
  bool valid{false};
};

struct AlgorithmDecompositionReflection {
  std::string algorithm_name;
  std::vector<std::string> required_resources;
  bool valid{false};
};

struct AlgorithmPackageDebugState {
  std::vector<codec::AdvancedAlgorithmDebugSignal> signals;
};

struct AlgorithmInterventionPackageDebugState {
  std::vector<codec::AdvancedAlgorithmDebugSignal> signals;
  AlgorithmToAgentSignal algorithm_to_agent_signal{};
};

struct AgentInterventionUiContext {
  const Agent* agent{nullptr};
  const Mesh* mesh{nullptr};
  const InputState* input{nullptr};
  Vec2 mouse_pixel{};
  float dt_seconds{0.0f};
  const AlgorithmToAgentSignal* algorithm_to_agent_signal{nullptr};
};

class IAlgorithmInterventionPackageUiState {
 public:
  virtual ~IAlgorithmInterventionPackageUiState() = default;
};

class IAlgorithmInterventionPackageUi {
 public:
  virtual ~IAlgorithmInterventionPackageUi() = default;

  virtual const char* title() const = 0;
  virtual std::unique_ptr<IAlgorithmInterventionPackageUiState> CreateUiState() const = 0;
  virtual void DrawUi(
    const AgentInterventionUiContext& context,
    IAlgorithmInterventionPackageUiState* ui_state) const = 0;
};

class IAlgorithmPackageCodec {
 public:
  virtual ~IAlgorithmPackageCodec() = default;

  virtual bool BuildContainerDescriptor(
    const codec::VolumeDescriptor& volume,
    AlgorithmContainerDescriptor* out_descriptor) const = 0;

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

  virtual bool ReflectReadableParameters(
    const AlgorithmContainerDescriptor& container_descriptor,
    AlgorithmReadableReflection* out_reflection) const {
    (void)container_descriptor;
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

class IAlgorithmPackageDecomposer {
 public:
  virtual ~IAlgorithmPackageDecomposer() = default;

  virtual bool ReflectDecomposition(
    const AlgorithmContainerDescriptor& container_descriptor,
    AlgorithmDecompositionReflection* out_reflection) const {
    (void)container_descriptor;
    (void)out_reflection;
    return false;
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

class IAlgorithmInterventionPackageCodec {
 public:
  virtual ~IAlgorithmInterventionPackageCodec() = default;

  virtual bool BuildInterventionPacket(
    const InteractionInterventionRequest& request,
    IoBufferPacket* packet) const = 0;
  virtual bool DecodeInterventionPacket(
    const IoBufferPacket& packet,
    InteractionInterventionRequest* request) const = 0;
};

class IAlgorithmInterventionPackageAgent {
 public:
  virtual ~IAlgorithmInterventionPackageAgent() = default;

  virtual bool NeedsIntervention(const AgentToAlgorithmSignal& signal) const = 0;
  virtual bool ShouldPause(const AgentToAlgorithmSignal& signal) const = 0;
};

class IAlgorithmInterventionPackageAlgorithm {
 public:
  virtual ~IAlgorithmInterventionPackageAlgorithm() = default;

  virtual bool SupportsIntervention() const = 0;
};

struct AgentAlgorithmPackageHandle {
  std::string package_name;
  std::shared_ptr<IAlgorithmPackageCodec> codec_hook;
  std::shared_ptr<IAlgorithmPackageDecomposer> decomposer_hook;
};

struct AgentInterventionPackageHandle {
  std::string package_name;
  std::shared_ptr<IAlgorithmInterventionPackageCodec> codec_hook;
  std::shared_ptr<IAlgorithmInterventionPackageAgent> agent_hook;
  std::shared_ptr<IAlgorithmInterventionPackageAlgorithm> algorithm_hook;
  std::shared_ptr<IAlgorithmInterventionPackageUi> ui_hook;
};

struct AgentInitConfig {
  std::string agent_name;
  std::string algorithm_name;
  std::vector<AgentAlgorithmPackageHandle> compliance_packages;
  std::shared_ptr<AgentInterventionPackageHandle> intervention_package;
  AlgorithmDataContract resource_descriptor{};
  AlgorithmComplianceDescriptor compliance_descriptor{};
};

class Agent {
 public:
  bool Init(AgentInitConfig config) {
    initialized_ = true;
    agent_name_ = std::move(config.agent_name);
    compliance_packages_ = std::move(config.compliance_packages);
    intervention_package_ = std::move(config.intervention_package);
    algorithm_name_ = std::move(config.algorithm_name);
    resource_descriptor_ = std::move(config.resource_descriptor);
    parameter_descriptor_ = std::move(config.compliance_descriptor);
    if (resource_descriptor_.arrays_to_allocate.empty() &&
        resource_descriptor_.temporary_registers_to_allocate.empty() &&
        resource_descriptor_.temporary_caches_to_allocate.empty() &&
        resource_descriptor_.filled_data_formats.empty() &&
        resource_descriptor_.algorithm_required_formats.empty() &&
        resource_descriptor_.container_aliases.empty()) {
      resource_descriptor_ = parameter_descriptor_.data_contract;
    }
    return true;
  }

  void Destroy() {
    initialized_ = false;
    agent_name_.clear();
    algorithm_name_.clear();
    compliance_packages_.clear();
    intervention_package_.reset();
    resource_descriptor_ = {};
    parameter_descriptor_ = {};
  }

  bool initialized() const { return initialized_; }
  const std::string& agent_name() const { return agent_name_; }
  const std::string& algorithm_name() const { return algorithm_name_; }
  const std::vector<AgentAlgorithmPackageHandle>& compliance_packages() const { return compliance_packages_; }
  const std::vector<AgentAlgorithmPackageHandle>& algorithm_packages() const { return compliance_packages_; }
  const AgentAlgorithmPackageHandle* FindAlgorithmPackage(const std::string& package_name) const;
  bool HasAlgorithmPackage(const std::string& package_name) const;
  const std::shared_ptr<AgentInterventionPackageHandle>& intervention_package() const { return intervention_package_; }
  const AlgorithmDataContract& resource_descriptor() const { return resource_descriptor_; }
  const AlgorithmComplianceDescriptor& parameter_descriptor() const { return parameter_descriptor_; }
  const AlgorithmComplianceDescriptor& compliance_descriptor() const { return parameter_descriptor_; }

 private:
  bool initialized_{false};
  std::string algorithm_name_{};
  std::string agent_name_{};
  std::vector<AgentAlgorithmPackageHandle> compliance_packages_{};
  std::shared_ptr<AgentInterventionPackageHandle> intervention_package_{};
  AlgorithmDataContract resource_descriptor_{};
  AlgorithmComplianceDescriptor parameter_descriptor_{};
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
