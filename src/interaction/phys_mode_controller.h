#pragma once

#include "interaction_state.h"
#include "../mesh.h"
#include "../physics/physics_module.h"
#include "../viewport_transform.h"
#include "../win32_window.h"

#include <vector>

class PhysModeController {
 public:
  void PhysInit(const Mesh& mesh);
  void SetSubMode(PhysSubMode sub_mode) { sub_mode_ = sub_mode; }
  PhysSubMode sub_mode() const { return sub_mode_; }
  const std::vector<PhysDirective>& directives() const { return solver_.directives(); }

  InteractionFrame Tick(Mesh& mesh, const ViewportTransform& viewport, const InputState& input, Vec2 mouse_pixel);

 private:
  int dragging_anchor_{-1};
  Vec3 drag_start_{};
  bool initialized_{false};
  PhysSubMode sub_mode_{PhysSubMode::Guide};
  PhysicsSolver solver_;

  int FindHoveredVertex(const Mesh& mesh, const ViewportTransform& viewport, Vec2 mouse_pixel) const;
};
