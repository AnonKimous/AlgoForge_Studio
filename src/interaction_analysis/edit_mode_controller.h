#pragma once

#include "data_protocol/interaction/interaction_state.h"
#include "data_protocol/mesh.h"
#include "service_domains/render/scene_camera.h"
#include "foundation/viewport_transform.h"
#include "foundation/input_state.h"

namespace interaction_analysis {

class EditModeController {
 public:
  InteractionFrame Tick(Mesh& mesh, const ViewportTransform& viewport, const SceneCamera& camera, const InputState& input, Vec2 mouse_pixel);

 private:
  int dragging_vertex_{-1};
  SelectionState selection_{};

  int FindHoveredVertex(const Mesh& mesh, const ViewportTransform& viewport, const SceneCamera& camera, Vec2 mouse_pixel) const;
  int FindTriangleAt(const Mesh& mesh, const SceneCamera& camera, const ViewportTransform& viewport, Vec2 mouse_pixel) const;
};

}  // namespace interaction_analysis
