#pragma once

#include "interaction_state.h"
#include "../mesh.h"
#include "../render/scene_camera.h"
#include "../viewport_transform.h"
#include "window/input_state.h"

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
