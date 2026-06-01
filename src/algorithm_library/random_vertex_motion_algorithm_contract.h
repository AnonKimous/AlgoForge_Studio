#pragma once

#include "../algorithm/algorithm_types.h"

#include "common_data/mesh.h"

namespace algorithm_library {

inline constexpr const char* kRandomVertexMotionAlgorithmName = "random_vertex_motion";

AlgorithmComplianceDescriptor CreateRandomVertexMotionAlgorithmComplianceDescriptor(
  const Mesh& mesh,
  float motion_radius);

bool RandomVertexMotionAlgorithm_Run(const PhysicsAlgorithmRequest& request, PhysicsAlgorithmResult* result);

}  // namespace algorithm_library

using algorithm_library::CreateRandomVertexMotionAlgorithmComplianceDescriptor;
using algorithm_library::kRandomVertexMotionAlgorithmName;
using algorithm_library::RandomVertexMotionAlgorithm_Run;
