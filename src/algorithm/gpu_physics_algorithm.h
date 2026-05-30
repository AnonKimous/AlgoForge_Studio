#pragma once

#include "algorithm_types.h"

namespace algorithm {

bool GpuPhysicsAlgorithm_Run(const PhysicsAlgorithmRequest& request, PhysicsAlgorithmResult* result);

}  // namespace algorithm

using algorithm::GpuPhysicsAlgorithm_Run;
