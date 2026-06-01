#pragma once

#include "algorithm_types.h"
#include "../algorithm/physics_algorithm_pipeline.h"

namespace algorithm_library {

inline bool AlgorithmMng_Run(const PhysicsAlgorithmRequest& request, PhysicsAlgorithmResult* result) {
  return PhysicsAlgorithmPipeline_Run(request, result);
}

}  // namespace algorithm_library

using algorithm_library::AlgorithmMng_Run;
