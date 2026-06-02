#include "algorithm_pool.h"

#include "algorithm_library/algorithm_mng.h"

#include <utility>

namespace agents {

void AlgorithmPool::Init(
  const PhysSolverConfig& config,
  const VulkanComputeContextView& compute_context,
  const AlgorithmContainerDescriptor& container_descriptor) {
  config_ = config;
  compute_context_ = compute_context;
  container_descriptor_ = container_descriptor;
}

bool AlgorithmPool::Run(const PhysicsAlgorithmRequest& request, PhysicsAlgorithmResult* result) const {
  return AlgorithmMng_Run(request, result);
}

void AlgorithmPool::SetInterventionPackage(std::shared_ptr<algorithm::AlgorithmInterventionPackageHandle> package) {
  intervention_package_ = std::move(package);
}

}  // namespace agents
