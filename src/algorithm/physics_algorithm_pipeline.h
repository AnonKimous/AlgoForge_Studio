#pragma once

#include "algorithm_types.h"

namespace algorithm {

bool PhysicsAlgorithmPipeline_Run(const PhysicsAlgorithmRequest& request, PhysicsAlgorithmResult* result);

}  // namespace algorithm

using algorithm::PhysicsAlgorithmPipeline_Run;
