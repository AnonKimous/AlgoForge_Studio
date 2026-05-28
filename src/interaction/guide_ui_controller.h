#pragma once

#include "../mesh.h"
#include "../viewport_transform.h"
#include "window/input_state.h"

#include <vector>

struct GuideUiFrame {
  int hovered_vertex{-1};
  std::vector<int> selected_vertices;
  bool dragging{false};
  int dragging_vertex{-1};
  Vec3 drag_target{};
};

class GuideUiController {
 public:
  GuideUiFrame Tick(const Mesh& mesh, const ViewportTransform& viewport, const InputState& input, Vec2 mouse_pixel);
  void ClearSelection();

 private:
  int dragging_vertex_{-1};
  std::vector<int> selected_vertices_;

  int FindHoveredVertex(const Mesh& mesh, const ViewportTransform& viewport, Vec2 mouse_pixel) const;
  static bool ContainsVertex(const std::vector<int>& vertices, int vertex);
};
