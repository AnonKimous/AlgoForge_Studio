#pragma once

#include "../mesh.h"
#include "../triangle_orientation_state.h"

bool RunTriangleOrientationCpuAlgorithm(
  const Mesh& current_mesh,
  const Mesh& reference_mesh,
  TriangleOrientationState* result);
