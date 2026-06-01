#pragma once

#include "algorithm_library/algorithm_package.h"
#include "algorithm_library/algorithm_types.h"

#include <memory>

namespace agents {

class AlgorithmPool {
 public:
  void Init(
    const PhysSolverConfig& config,
    const VulkanComputeContextView& compute_context,
    const AlgorithmComplianceDescriptor& compliance_descriptor);

  bool Run(const PhysicsAlgorithmRequest& request, PhysicsAlgorithmResult* result) const;

  void SetInterventionPackage(std::shared_ptr<algorithm::AlgorithmInterventionPackageHandle> package);

  const PhysSolverConfig& config() const { return config_; }
  const VulkanComputeContextView& compute_context() const { return compute_context_; }
  const AlgorithmComplianceDescriptor& compliance_descriptor() const { return compliance_descriptor_; }
  const std::shared_ptr<algorithm::AlgorithmInterventionPackageHandle>& intervention_package() const { return intervention_package_; }

 private:
  PhysSolverConfig config_{};
  VulkanComputeContextView compute_context_{};
  AlgorithmComplianceDescriptor compliance_descriptor_{};
  std::shared_ptr<algorithm::AlgorithmInterventionPackageHandle> intervention_package_{};
};

}  // namespace agents

using agents::AlgorithmPool;
