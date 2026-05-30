#pragma once

#include "data_protocol/mesh.h"
#include "data_protocol/triangle_orientation_state.h"

namespace algorithm {

bool RunTriangleOrientationCpuAlgorithm(
  const Mesh& current_mesh,
  const Mesh& reference_mesh,
  TriangleOrientationState* result);

}  // namespace algorithm

using algorithm::RunTriangleOrientationCpuAlgorithm;
