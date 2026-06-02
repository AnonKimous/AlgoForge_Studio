#pragma once

#include "algorithm_library/algorithm_package.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace agent {

using AgentAlgorithmPackageHandle = algorithm::AlgorithmPackageHandle;
using AgentInterventionPackageHandle = algorithm::AlgorithmInterventionPackageHandle;
using AgentDecomposerCodec = algorithm::IAlgorithmPackageCodec;
using AgentReflectorCodec = algorithm::IAlgorithmPackageCodec;

struct AgentPackageBinding {
  std::string package_name;
  std::vector<std::string> input_containers;
  std::vector<std::string> output_containers;
  std::vector<algorithm::AlgorithmContainerAlias> container_aliases;
};

struct AgentContainerRoute {
  std::string source_package_name;
  std::string source_container_name;
  std::string target_package_name;
  std::string target_container_name;
};

struct AgentPipelineDescriptor {
  std::vector<AgentPackageBinding> ordered_bindings;
  std::vector<AgentContainerRoute> container_routes;
  std::vector<AlgorithmComplianceDescriptor> component_descriptors;
};

struct AgentInitConfig {
  std::string agent_name;
  std::string algorithm_name;
  std::string mounted_agent_name;
  std::vector<std::string> bound_resources;
  std::vector<AgentAlgorithmPackageHandle> compliance_packages;
  AgentPipelineDescriptor pipeline_descriptor;
  std::shared_ptr<AgentInterventionPackageHandle> intervention_package;
  std::shared_ptr<AgentDecomposerCodec> decomposer;
  std::shared_ptr<AgentReflectorCodec> reflector;
  PhysSolverConfig solver_config{};
  AlgorithmComplianceDescriptor compliance_descriptor{};
};

class Agent {
 public:
  bool Init(AgentInitConfig config) {
    initialized_ = true;
    agent_name_ = std::move(config.agent_name);
    compliance_packages_ = std::move(config.compliance_packages);
    pipeline_descriptor_ = std::move(config.pipeline_descriptor);
    intervention_package_ = std::move(config.intervention_package);
    decomposer_ = std::move(config.decomposer);
    reflector_ = std::move(config.reflector);
    algorithm_name_ = std::move(config.algorithm_name);
    mounted_agent_name_ = std::move(config.mounted_agent_name);
    bound_resources_ = std::move(config.bound_resources);
    solver_config_ = std::move(config.solver_config);
    compliance_descriptor_ = std::move(config.compliance_descriptor);
    return true;
  }

  void Destroy() {
    initialized_ = false;
    agent_name_.clear();
    algorithm_name_.clear();
    mounted_agent_name_.clear();
    bound_resources_.clear();
    compliance_packages_.clear();
    pipeline_descriptor_ = {};
    intervention_package_.reset();
    decomposer_.reset();
    reflector_.reset();
    solver_config_ = {};
    compliance_descriptor_ = {};
  }

  bool initialized() const { return initialized_; }
  const std::string& agent_name() const { return agent_name_; }
  const std::string& algorithm_name() const { return algorithm_name_; }
  const std::string& mounted_agent_name() const { return mounted_agent_name_; }
  const std::vector<std::string>& bound_resources() const { return bound_resources_; }
  const std::vector<AgentAlgorithmPackageHandle>& compliance_packages() const { return compliance_packages_; }
  const std::vector<AgentAlgorithmPackageHandle>& algorithm_packages() const { return compliance_packages_; }
  const AgentAlgorithmPackageHandle* FindAlgorithmPackage(const std::string& package_name) const;
  bool HasAlgorithmPackage(const std::string& package_name) const;
  const AgentPipelineDescriptor& pipeline_descriptor() const { return pipeline_descriptor_; }
  const AgentPackageBinding* FindPackageBinding(const std::string& package_name) const;
  const std::shared_ptr<AgentInterventionPackageHandle>& intervention_package() const { return intervention_package_; }
  const std::shared_ptr<AgentDecomposerCodec>& decomposer() const { return decomposer_; }
  const std::shared_ptr<AgentReflectorCodec>& reflector() const { return reflector_; }
  const PhysSolverConfig& solver_config() const { return solver_config_; }
  const AlgorithmComplianceDescriptor& compliance_descriptor() const { return compliance_descriptor_; }
  const AlgorithmComplianceDescriptor& composite_compliance_descriptor() const { return compliance_descriptor_; }

 private:
  bool initialized_{false};
  std::string algorithm_name_{};
  std::string agent_name_{};
  std::string mounted_agent_name_{};
  std::vector<std::string> bound_resources_{};
  std::vector<AgentAlgorithmPackageHandle> compliance_packages_{};
  AgentPipelineDescriptor pipeline_descriptor_{};
  std::shared_ptr<AgentInterventionPackageHandle> intervention_package_{};
  std::shared_ptr<AgentDecomposerCodec> decomposer_{};
  std::shared_ptr<AgentReflectorCodec> reflector_{};
  PhysSolverConfig solver_config_{};
  AlgorithmComplianceDescriptor compliance_descriptor_{};
};

using AgentRuntime = Agent;

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

using agent::Agent;
using agent::AgentAlgorithmPackageHandle;
using agent::AgentContainerRoute;
using agent::AgentDecomposerCodec;
using agent::AgentInitConfig;
using agent::AgentInterventionPackageHandle;
using agent::AgentPackageBinding;
using agent::AgentPipelineDescriptor;
using agent::AgentReflectorCodec;
using agent::AgentRuntime;
using agent::agent_destroy;
using agent::agent_init;
