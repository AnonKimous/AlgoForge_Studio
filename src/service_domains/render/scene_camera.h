#pragma once

#include "common_data/vector_types.h"
#include "common_data/mesh.h"
#include "common_data/viewport_transform.h"

#include <Eigen/Dense>

namespace service_domains {

class SceneCamera {
 public:
  void FitToMesh(const Mesh& mesh);
  void SetViewportSize(int width, int height);

  Eigen::Matrix4f ModelMatrix() const;
  Eigen::Matrix4f ViewMatrix() const;
  Eigen::Matrix4f ProjectionMatrix() const;
  Eigen::Matrix4f MvpMatrix() const;

  Vec2 WorldToWindow(Vec3 world, const ViewportTransform& viewport) const;
  Vec3 WindowToWorld(Vec2 mouse_pixel, const ViewportTransform& viewport, float world_z = 0.0f) const;
  Vec2 WorldToNdc(Vec3 world) const;

 private:
  void RebuildFraming();

  Vec3 center_{};
  float base_half_width_{1.0f};
  float base_half_height_{1.0f};
  float base_half_depth_{1.0f};
  float visible_half_width_{1.0f};
  float visible_half_height_{1.0f};
  float visible_half_depth_{1.0f};
  float padding_{1.15f};
  int viewport_width_{1};
  int viewport_height_{1};
  float world_units_per_pixel_{0.0f};
  bool pixel_scale_locked_{false};
};

}  // namespace service_domains

using service_domains::SceneCamera;
