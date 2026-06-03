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

}  // namespace agent
