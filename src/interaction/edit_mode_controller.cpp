#include "edit_mode_controller.h"

#include <cmath>

namespace {

float Cross(Vec2 a, Vec2 b) {
  return a.x * b.y - a.y * b.x;
}

bool PointInTriangle(Vec2 p, Vec2 a, Vec2 b, Vec2 c) {
  float c0 = Cross(Vec2{b.x - a.x, b.y - a.y}, Vec2{p.x - a.x, p.y - a.y});
  float c1 = Cross(Vec2{c.x - b.x, c.y - b.y}, Vec2{p.x - b.x, p.y - b.y});
  float c2 = Cross(Vec2{a.x - c.x, a.y - c.y}, Vec2{p.x - c.x, p.y - c.y});
  bool has_negative = c0 < 0.0f || c1 < 0.0f || c2 < 0.0f;
  bool has_positive = c0 > 0.0f || c1 > 0.0f || c2 > 0.0f;
  return !(has_negative && has_positive);
}

}  // namespace

InteractionFrame EditModeController::Tick(Mesh& mesh, const ViewportTransform& viewport, const InputState& input, Vec2 mouse_pixel) {
  Vec2 mouse_ndc = viewport.WindowToNdc(mouse_pixel);
  int hovered_vertex = FindHoveredVertex(mesh, viewport, mouse_pixel);

  if (input.left_pressed) {
    if (hovered_vertex >= 0) {
      dragging_vertex_ = hovered_vertex;
      selection_ = SelectionState{SelectionKind::Vertex, hovered_vertex, -1};
    } else {
      int triangle = FindTriangleAt(mesh, mouse_ndc);
      dragging_vertex_ = -1;
      selection_ = triangle >= 0 ? SelectionState{SelectionKind::Triangle, -1, triangle} : SelectionState{};
    }
  }

  if (input.left_released) {
    dragging_vertex_ = -1;
  }

  if (input.left_down && dragging_vertex_ >= 0) {
    mesh.positions[dragging_vertex_].x = mouse_ndc.x;
    mesh.positions[dragging_vertex_].y = mouse_ndc.y;
    selection_ = SelectionState{SelectionKind::Vertex, dragging_vertex_, -1};
    hovered_vertex = dragging_vertex_;
  }

  return InteractionFrame{hovered_vertex, selection_};
}

int EditModeController::FindHoveredVertex(const Mesh& mesh, const ViewportTransform& viewport, Vec2 mouse_pixel) const {
  int highlighted = -1;
  float best_pixels2 = 22.0f * 22.0f;
  for (uint32_t i = 0; i < mesh.positions.size(); ++i) {
    Vec2 vertex_pixel = viewport.NdcToWindow(Vec2{mesh.positions[i].x, mesh.positions[i].y});
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

int EditModeController::FindTriangleAt(const Mesh& mesh, Vec2 mouse_ndc) const {
  for (uint32_t i = 0; i < mesh.triangles.size(); ++i) {
    const auto& tri = mesh.triangles[i];
    Vec2 a{mesh.positions[tri[0]].x, mesh.positions[tri[0]].y};
    Vec2 b{mesh.positions[tri[1]].x, mesh.positions[tri[1]].y};
    Vec2 c{mesh.positions[tri[2]].x, mesh.positions[tri[2]].y};
    if (PointInTriangle(mouse_ndc, a, b, c)) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

