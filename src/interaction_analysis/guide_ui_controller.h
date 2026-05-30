#pragma once

#include "data_protocol/interaction/interaction_state.h"
#include "data_protocol/mesh.h"
#include "service_domains/render/scene_camera.h"
#include "foundation/viewport_transform.h"
#include "foundation/input_state.h"

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
