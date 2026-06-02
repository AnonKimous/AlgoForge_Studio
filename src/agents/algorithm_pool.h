#pragma once

#include "algorithm_library/algorithm_package.h"
#include "algorithm_library/algorithm_types.h"

#include <memory>

namespace agent_execute {

class AlgorithmPool {
 public:
  void Init(
    const PhysSolverConfig& config,
    const VulkanComputeContextView& compute_context,
    const AlgorithmContainerDescriptor& container_descriptor);

  bool Run(const PhysicsAlgorithmRequest& request, PhysicsAlgorithmResult* result) const;

  void SetInterventionPackage(std::shared_ptr<algorithm::AlgorithmInterventionPackageHandle> package);

  const PhysSolverConfig& config() const { return config_; }
  const VulkanComputeContextView& compute_context() const { return compute_context_; }
  const AlgorithmContainerDescriptor& container_descriptor() const { return container_descriptor_; }
  const std::shared_ptr<algorithm::AlgorithmInterventionPackageHandle>& intervention_package() const { return intervention_package_; }

 private:
  PhysSolverConfig config_{};
  VulkanComputeContextView compute_context_{};
  AlgorithmContainerDescriptor container_descriptor_{};
  std::shared_ptr<algorithm::AlgorithmInterventionPackageHandle> intervention_package_{};
};

}  // namespace agent_execute

using agent_execute::AlgorithmPool;
