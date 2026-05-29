#pragma once

#include "../interaction/interaction_state.h"
#include "../mesh.h"
#include "../render/renderer.h"
#include "../triangle_orientation_state.h"
#include "../validation_layer/validation_snapshot.h"

namespace interaction_analysis {

ValidationFrameSnapshot BuildValidationFrameSnapshot(
  const Mesh& mesh,
  const Mesh& reference_mesh,
  const TriangleOrientationState& orientation_state,
  const RenderUiState& ui_state,
  const RenderFrameResult& frame_result,
  int highlighted_vertex,
  float frame_dt_seconds);

}  // namespace interaction_analysis
