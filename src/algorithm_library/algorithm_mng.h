#pragma once

#include "algorithm_types.h"
#include "../algorithm/physics_algorithm_pipeline.h"

namespace algorithm_library {

inline bool AlgorithmMng_Run(const PhysicsAlgorithmRequest& request, PhysicsAlgorithmResult* result) {
  return PhysicsAlgorithmPipeline_Run(request, result);
}

inline std::string AlgorithmMng_ResolveContainerName(
  const AlgorithmComplianceDescriptor& compliance_descriptor,
  const std::string& package_name,
  const std::string& source_name) {
  return ResolveAlgorithmContainerName(compliance_descriptor, package_name, source_name);
}

inline std::string AlgorithmMng_ResolveContainerName(
  const PhysicsAlgorithmRequest& request,
  const std::string& package_name,
  const std::string& source_name) {
  return ResolveAlgorithmContainerName(request.compliance_descriptor, package_name, source_name);
}

}  // namespace algorithm_library

using algorithm_library::AlgorithmMng_Run;
using algorithm_library::AlgorithmMng_ResolveContainerName;
