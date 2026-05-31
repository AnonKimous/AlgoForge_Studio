#include "scene_camera.h"

#include "common_data/render_algorithm/camera_math.h"

#include <algorithm>
#include <cmath>

namespace service_domains {

void SceneCamera::FitToMesh(const Mesh& mesh) {
  pixel_scale_locked_ = false;

  if (mesh.positions.empty()) {
    center_ = Vec3{};
    base_half_width_ = 1.0f;
    base_half_height_ = 1.0f;
    base_half_depth_ = 1.0f;
    RebuildFraming();
    return;
  }

  Vec3 min_pos = mesh.positions.front();
  Vec3 max_pos = mesh.positions.front();
  for (const Vec3& position : mesh.positions) {
    min_pos.x = std::min(min_pos.x, position.x);
    min_pos.y = std::min(min_pos.y, position.y);
    min_pos.z = std::min(min_pos.z, position.z);
    max_pos.x = std::max(max_pos.x, position.x);
    max_pos.y = std::max(max_pos.y, position.y);
    max_pos.z = std::max(max_pos.z, position.z);
  }

  center_ = Vec3{
    (min_pos.x + max_pos.x) * 0.5f,
    (min_pos.y + max_pos.y) * 0.5f,
    (min_pos.z + max_pos.z) * 0.5f,
  };

  base_half_width_ = std::max((max_pos.x - min_pos.x) * 0.5f, 0.001f);
  base_half_height_ = std::max((max_pos.y - min_pos.y) * 0.5f, 0.001f);
  base_half_depth_ = std::max((max_pos.z - min_pos.z) * 0.5f, 1.0f);
  RebuildFraming();
}

void SceneCamera::SetViewportSize(int width, int height) {
  viewport_width_ = std::max(width, 1);
  viewport_height_ = std::max(height, 1);
  RebuildFraming();
}

Eigen::Matrix4f SceneCamera::ModelMatrix() const {
  return Eigen::Matrix4f::Identity();
}

Eigen::Matrix4f SceneCamera::ViewMatrix() const {
  return RenderAlgorithm_BuildCenteredViewMatrix(center_);
}

Eigen::Matrix4f SceneCamera::ProjectionMatrix() const {
  return RenderAlgorithm_BuildOrthoProjectionMatrix(visible_half_width_, visible_half_height_, visible_half_depth_);
}

Eigen::Matrix4f SceneCamera::MvpMatrix() const {
  return ProjectionMatrix() * ViewMatrix() * ModelMatrix();
}

Vec2 SceneCamera::WorldToWindow(Vec3 world, const ViewportTransform& viewport) const {
  return RenderAlgorithm_WorldToWindow(world, center_, visible_half_width_, visible_half_height_, viewport.width(), viewport.height());
}

Vec3 SceneCamera::WindowToWorld(Vec2 mouse_pixel, const ViewportTransform& viewport, float world_z) const {
  return RenderAlgorithm_WindowToWorld(mouse_pixel, center_, visible_half_width_, visible_half_height_, viewport.width(), viewport.height(), world_z);
}

Vec2 SceneCamera::WorldToNdc(Vec3 world) const {
  return RenderAlgorithm_WorldToNdc(world, center_, visible_half_width_, visible_half_height_);
}

void SceneCamera::RebuildFraming() {
  const float padded_half_width = base_half_width_ * padding_;
  const float padded_half_height = base_half_height_ * padding_;

  if (!pixel_scale_locked_) {
    if (viewport_width_ <= 1 || viewport_height_ <= 1) {
      visible_half_width_ = padded_half_width;
      visible_half_height_ = padded_half_height;
      visible_half_depth_ = std::max(base_half_depth_, 1.0f);
      return;
    }

    const float half_viewport_width = std::max(static_cast<float>(viewport_width_) * 0.5f, 1.0f);
    const float half_viewport_height = std::max(static_cast<float>(viewport_height_) * 0.5f, 1.0f);
    world_units_per_pixel_ = std::max(
      padded_half_width / half_viewport_width,
      padded_half_height / half_viewport_height);
    if (!(std::isfinite(world_units_per_pixel_) && world_units_per_pixel_ > 0.0f)) {
      world_units_per_pixel_ = 1.0f;
    }
    pixel_scale_locked_ = true;
  }

  visible_half_width_ = world_units_per_pixel_ * static_cast<float>(viewport_width_) * 0.5f;
  visible_half_height_ = world_units_per_pixel_ * static_cast<float>(viewport_height_) * 0.5f;
  visible_half_depth_ = std::max(base_half_depth_, 1.0f);
}

}  // namespace service_domains
