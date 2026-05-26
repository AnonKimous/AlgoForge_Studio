#include "phys_mode_controller.h"

void PhysModeController::PhysInit(const Mesh& mesh) {
  solver_.Init(mesh);
  initialized_ = true;
  sub_mode_ = PhysSubMode::Guide;
}

InteractionFrame PhysModeController::Tick(Mesh& mesh, const ViewportTransform& viewport, const InputState& input, Vec2 mouse_pixel) {
  if (!initialized_) {
    PhysInit(mesh);
  }

  Vec2 mouse_ndc = viewport.WindowToNdc(mouse_pixel);
  int hovered_vertex = FindHoveredVertex(mesh, viewport, mouse_pixel);

  if (sub_mode_ == PhysSubMode::Run) {
    solver_.ApplyValidDirectives(mesh);
    return InteractionFrame{hovered_vertex, SelectionState{}};
  }

  if (input.left_pressed && hovered_vertex >= 0) {
    dragging_anchor_ = hovered_vertex;
    drag_start_ = mesh.positions[hovered_vertex];
  }

  if (input.left_down && dragging_anchor_ >= 0) {
    Vec3 requested{mouse_ndc.x, mouse_ndc.y, mesh.positions[dragging_anchor_].z};
    solver_.UpsertDirective(mesh, dragging_anchor_, drag_start_, requested);
  }

  if (input.left_released && dragging_anchor_ >= 0) {
    dragging_anchor_ = -1;
  }

  return InteractionFrame{hovered_vertex, hovered_vertex >= 0 ? SelectionState{SelectionKind::Vertex, hovered_vertex, -1} : SelectionState{}};
}

int PhysModeController::FindHoveredVertex(const Mesh& mesh, const ViewportTransform& viewport, Vec2 mouse_pixel) const {
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
