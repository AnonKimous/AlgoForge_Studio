#pragma once

#include "interaction_state.h"
#include "../mesh.h"
#include "../render/scene_camera.h"
#include "../viewport_transform.h"
#include "window/input_state.h"

#include <vector>

namespace interaction_analysis {

class GuideUiController {
 public:
  GuideUiFrame Tick(const Mesh& mesh, const ViewportTransform& viewport, const SceneCamera& camera, const InputState& input, Vec2 mouse_pixel);
  void ClearSelection();

 private:
  int dragging_vertex_{-1};
  std::vector<int> selected_vertices_;

  int FindHoveredVertex(const Mesh& mesh, const ViewportTransform& viewport, const SceneCamera& camera, Vec2 mouse_pixel) const;
  static bool ContainsVertex(const std::vector<int>& vertices, int vertex);
};

}  // namespace interaction_analysis
