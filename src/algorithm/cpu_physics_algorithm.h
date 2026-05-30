#pragma once

#include "algorithm_types.h"

namespace algorithm {

bool CpuPhysicsAlgorithm_Run(const PhysicsAlgorithmRequest& request, PhysicsAlgorithmResult* result);

}  // namespace algorithm

using algorithm::CpuPhysicsAlgorithm_Run;
