#include "entity.h"

namespace orchestration_entity {

const OrchestrationEntityAlgorithmPackageHandle* OrchestrationEntity::FindAlgorithmPackage(const std::string& package_name) const {
  for (const auto& package : compliance_packages_) {
    if (package.package_name == package_name) {
      return &package;
    }
  }
  return nullptr;
}

bool OrchestrationEntity::HasAlgorithmPackage(const std::string& package_name) const {
  return FindAlgorithmPackage(package_name) != nullptr;
}

const OrchestrationEntityPackageBinding* OrchestrationEntity::FindPackageBinding(const std::string& package_name) const {
  for (const auto& binding : pipeline_descriptor_.ordered_bindings) {
    if (binding.package_name == package_name) {
      return &binding;
    }
  }
  return nullptr;
}

}  // namespace orchestration_entity
