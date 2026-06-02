#include "agent.h"

namespace agent {

const AgentAlgorithmPackageHandle* Agent::FindAlgorithmPackage(const std::string& package_name) const {
  for (const auto& package : compliance_packages_) {
    if (package.package_name == package_name) {
      return &package;
    }
  }
  return nullptr;
}

bool Agent::HasAlgorithmPackage(const std::string& package_name) const {
  return FindAlgorithmPackage(package_name) != nullptr;
}

const AgentPackageBinding* Agent::FindPackageBinding(const std::string& package_name) const {
  for (const auto& binding : pipeline_descriptor_.ordered_bindings) {
    if (binding.package_name == package_name) {
      return &binding;
    }
  }
  return nullptr;
}

}  // namespace agent
