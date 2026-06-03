#pragma once

#include "algorithm_library/algorithm_package.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace agent {

using AgentAlgorithmPackageHandle = algorithm::AlgorithmPackageHandle;
using AgentInterventionPackageHandle = algorithm::AlgorithmInterventionPackageHandle;

struct AgentInitConfig {
  std::string agent_name;
  std::string algorithm_name;
  std::vector<AgentAlgorithmPackageHandle> compliance_packages;
  std::shared_ptr<AgentInterventionPackageHandle> intervention_package;
  PhysSolverConfig solver_config{};
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
    solver_config_ = std::move(config.solver_config);
    compliance_descriptor_ = std::move(config.compliance_descriptor);
    return true;
  }

  void Destroy() {
    initialized_ = false;
    agent_name_.clear();
    algorithm_name_.clear();
    compliance_packages_.clear();
    intervention_package_.reset();
    solver_config_ = {};
    compliance_descriptor_ = {};
  }

  bool initialized() const { return initialized_; }
  const std::string& agent_name() const { return agent_name_; }
  const std::string& algorithm_name() const { return algorithm_name_; }
  const std::vector<AgentAlgorithmPackageHandle>& compliance_packages() const { return compliance_packages_; }
  const std::vector<AgentAlgorithmPackageHandle>& algorithm_packages() const { return compliance_packages_; }
  const AgentAlgorithmPackageHandle* FindAlgorithmPackage(const std::string& package_name) const;
  bool HasAlgorithmPackage(const std::string& package_name) const;
  const std::shared_ptr<AgentInterventionPackageHandle>& intervention_package() const { return intervention_package_; }
  const PhysSolverConfig& solver_config() const { return solver_config_; }
  const AlgorithmComplianceDescriptor& compliance_descriptor() const { return compliance_descriptor_; }

 private:
  bool initialized_{false};
  std::string algorithm_name_{};
  std::string agent_name_{};
  std::vector<AgentAlgorithmPackageHandle> compliance_packages_{};
  std::shared_ptr<AgentInterventionPackageHandle> intervention_package_{};
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
using agent::AgentInitConfig;
using agent::AgentInterventionPackageHandle;
using agent::AgentRuntime;
using agent::agent_destroy;
using agent::agent_init;
