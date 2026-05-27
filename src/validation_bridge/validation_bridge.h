#pragma once

#include "../interaction/interaction_state.h"
#include "../mesh.h"
#include "../render/renderer.h"
#include "../triangle_orientation_analyzer.h"
#include "../validation_layer/validation_snapshot.h"

ValidationFrameSnapshot BuildValidationFrameSnapshot(
  const Mesh& mesh,
  const Mesh& reference_mesh,
  const TriangleOrientationAnalyzer& analyzer,
  const RenderUiState& ui_state,
  const RenderFrameResult& frame_result,
  int highlighted_vertex,
  float frame_dt_seconds);
