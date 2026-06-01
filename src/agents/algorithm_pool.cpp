#include "algorithm_pool.h"

#include "algorithm_library/algorithm_mng.h"

#include <utility>

namespace agents {

void AlgorithmPool::Init(
  const PhysSolverConfig& config,
  const VulkanComputeContextView& compute_context,
  const AlgorithmComplianceDescriptor& compliance_descriptor) {
  config_ = config;
  compute_context_ = compute_context;
  compliance_descriptor_ = compliance_descriptor;
}

bool AlgorithmPool::Run(const PhysicsAlgorithmRequest& request, PhysicsAlgorithmResult* result) const {
  return AlgorithmMng_Run(request, result);
}

void AlgorithmPool::SetInterventionPackage(std::shared_ptr<algorithm::AlgorithmInterventionPackageHandle> package) {
  intervention_package_ = std::move(package);
}

}  // namespace agents
