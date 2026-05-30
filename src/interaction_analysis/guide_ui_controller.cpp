#include "guide_ui_controller.h"

#include <algorithm>
#include <cmath>

namespace {

void RemoveVertex(std::vector<int>& vertices, int vertex) {
  vertices.erase(std::remove(vertices.begin(), vertices.end(), vertex), vertices.end());
}

}  // namespace

namespace interaction_analysis {

GuideUiFrame GuideUiController::Tick(const Mesh& mesh, const ViewportTransform& viewport, const SceneCamera& camera, const InputState& input, Vec2 mouse_pixel) {
  GuideUiFrame frame{};
  frame.hovered_vertex = FindHoveredVertex(mesh, viewport, camera, mouse_pixel);
  frame.selected_vertices = selected_vertices_;

  if (input.left_pressed) {
    if (frame.hovered_vertex >= 0) {
      if (input.ctrl_down) {
        bool was_selected = ContainsVertex(selected_vertices_, frame.hovered_vertex);
        if (was_selected) {
          RemoveVertex(selected_vertices_, frame.hovered_vertex);
        } else {
          selected_vertices_.push_back(frame.hovered_vertex);
        }
        dragging_vertex_ = was_selected ? -1 : frame.hovered_vertex;
      } else {
        selected_vertices_.clear();
        selected_vertices_.push_back(frame.hovered_vertex);
        dragging_vertex_ = frame.hovered_vertex;
      }
    } else if (!input.ctrl_down) {
      selected_vertices_.clear();
      dragging_vertex_ = -1;
    }
  }

  if (input.left_released) {
    dragging_vertex_ = -1;
  }

  if (input.left_down && dragging_vertex_ >= 0) {
    frame.dragging = true;
    frame.dragging_vertex = dragging_vertex_;
    frame.drag_target = camera.WindowToWorld(mouse_pixel, viewport, mesh.positions[dragging_vertex_].z);
    if (!ContainsVertex(selected_vertices_, dragging_vertex_)) {
      selected_vertices_.push_back(dragging_vertex_);
    }
  }

  frame.selected_vertices = selected_vertices_;
  return frame;
}

void GuideUiController::ClearSelection() {
  selected_vertices_.clear();
  dragging_vertex_ = -1;
}

int GuideUiController::FindHoveredVertex(const Mesh& mesh, const ViewportTransform& viewport, const SceneCamera& camera, Vec2 mouse_pixel) const {
  int highlighted = -1;
  float best_pixels2 = 22.0f * 22.0f;
  for (uint32_t i = 0; i < mesh.positions.size(); ++i) {
    Vec2 vertex_pixel = camera.WorldToWindow(mesh.positions[i], viewport);
    float dx = vertex_pixel.x - mouse_pixel.x;
    float dy = vertex_pixel.y - mouse_pixel.y;
    float dist2 = dx * dx + dy * dy;
    if (dist2 <= best_pixels2) {
      best_pixels2 = dist2;
      highlighted = static_cast<int>(i);
    }
  }
  return highlighted;
}

bool GuideUiController::ContainsVertex(const std::vector<int>& vertices, int vertex) {
  return std::find(vertices.begin(), vertices.end(), vertex) != vertices.end();
}

}  // namespace interaction_analysis
