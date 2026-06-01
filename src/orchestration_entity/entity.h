#pragma once

#include "algorithm_library/algorithm_package.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace orchestration_entity {

using OrchestrationEntityAlgorithmPackageHandle = algorithm::AlgorithmPackageHandle;
using OrchestrationEntityInterventionPackageHandle = algorithm::AlgorithmInterventionPackageHandle;
using OrchestrationEntityDecomposerCodec = algorithm::IAlgorithmPackageCodec;
using OrchestrationEntityReflectorCodec = algorithm::IAlgorithmPackageCodec;

struct OrchestrationEntityInitConfig {
  std::string algorithm_name;
  std::string mounted_agent_name;
  std::vector<std::string> bound_resources;
  std::vector<OrchestrationEntityAlgorithmPackageHandle> compliance_packages;
  std::shared_ptr<OrchestrationEntityInterventionPackageHandle> intervention_package;
  std::shared_ptr<OrchestrationEntityDecomposerCodec> decomposer;
  std::shared_ptr<OrchestrationEntityReflectorCodec> reflector;
  PhysSolverConfig solver_config{};
  AlgorithmComplianceDescriptor compliance_descriptor{};
};

class OrchestrationEntity {
 public:
  bool Init(OrchestrationEntityInitConfig config) {
    initialized_ = true;
    compliance_packages_ = std::move(config.compliance_packages);
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
    algorithm_name_.clear();
    mounted_agent_name_.clear();
    bound_resources_.clear();
    compliance_packages_.clear();
    intervention_package_.reset();
    decomposer_.reset();
    reflector_.reset();
    solver_config_ = {};
    compliance_descriptor_ = {};
  }

  bool initialized() const { return initialized_; }
  const std::string& algorithm_name() const { return algorithm_name_; }
  const std::string& mounted_agent_name() const { return mounted_agent_name_; }
  const std::vector<std::string>& bound_resources() const { return bound_resources_; }
  const std::vector<OrchestrationEntityAlgorithmPackageHandle>& compliance_packages() const { return compliance_packages_; }
  const std::shared_ptr<OrchestrationEntityInterventionPackageHandle>& intervention_package() const { return intervention_package_; }
  const std::shared_ptr<OrchestrationEntityDecomposerCodec>& decomposer() const { return decomposer_; }
  const std::shared_ptr<OrchestrationEntityReflectorCodec>& reflector() const { return reflector_; }
  const PhysSolverConfig& solver_config() const { return solver_config_; }
  const AlgorithmComplianceDescriptor& compliance_descriptor() const { return compliance_descriptor_; }

 private:
  bool initialized_{false};
  std::string algorithm_name_{};
  std::string mounted_agent_name_{};
  std::vector<std::string> bound_resources_{};
  std::vector<OrchestrationEntityAlgorithmPackageHandle> compliance_packages_{};
  std::shared_ptr<OrchestrationEntityInterventionPackageHandle> intervention_package_{};
  std::shared_ptr<OrchestrationEntityDecomposerCodec> decomposer_{};
  std::shared_ptr<OrchestrationEntityReflectorCodec> reflector_{};
  PhysSolverConfig solver_config_{};
  AlgorithmComplianceDescriptor compliance_descriptor_{};
};

using OrchestrationEntityRuntime = OrchestrationEntity;

inline bool orchestration_entity_init(OrchestrationEntity* entity, OrchestrationEntityInitConfig config) {
  if (!entity) return false;
  return entity->Init(std::move(config));
}

inline void orchestration_entity_destroy(OrchestrationEntity* entity) {
  if (entity) {
    entity->Destroy();
  }
}

}  // namespace orchestration_entity

using orchestration_entity::OrchestrationEntity;
using orchestration_entity::OrchestrationEntityAlgorithmPackageHandle;
using orchestration_entity::OrchestrationEntityDecomposerCodec;
using orchestration_entity::OrchestrationEntityInitConfig;
using orchestration_entity::OrchestrationEntityInterventionPackageHandle;
using orchestration_entity::OrchestrationEntityReflectorCodec;
using orchestration_entity::OrchestrationEntityRuntime;
using orchestration_entity::orchestration_entity_destroy;
using orchestration_entity::orchestration_entity_init;
