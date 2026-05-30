#pragma once

#include "foundation/common/vector_types.h"

#include <Eigen/Dense>

namespace foundation {

inline Eigen::Matrix4f RenderAlgorithm_BuildCenteredViewMatrix(Vec3 center) {
  Eigen::Matrix4f view = Eigen::Matrix4f::Identity();
  view(0, 3) = -center.x;
  view(1, 3) = -center.y;
  view(2, 3) = -center.z;
  return view;
}

inline Eigen::Matrix4f RenderAlgorithm_BuildOrthoProjectionMatrix(float visible_half_width, float visible_half_height, float visible_half_depth) {
  Eigen::Matrix4f projection = Eigen::Matrix4f::Identity();
  projection(0, 0) = 1.0f / visible_half_width;
  projection(1, 1) = -1.0f / visible_half_height;
  projection(2, 2) = 1.0f / visible_half_depth;
  return projection;
}

inline Vec2 RenderAlgorithm_WorldToWindow(Vec3 world, Vec3 center, float visible_half_width, float visible_half_height, int viewport_width, int viewport_height) {
  float ndc_x = (world.x - center.x) / visible_half_width;
  float ndc_y = -(world.y - center.y) / visible_half_height;
  return Vec2{
    (ndc_x * 0.5f + 0.5f) * static_cast<float>(viewport_width),
    (ndc_y * 0.5f + 0.5f) * static_cast<float>(viewport_height),
  };
}

inline Vec3 RenderAlgorithm_WindowToWorld(Vec2 mouse_pixel, Vec3 center, float visible_half_width, float visible_half_height, int viewport_width, int viewport_height, float world_z) {
  float ndc_x = (mouse_pixel.x / static_cast<float>(viewport_width)) * 2.0f - 1.0f;
  float ndc_y = (mouse_pixel.y / static_cast<float>(viewport_height)) * 2.0f - 1.0f;
  return Vec3{
    center.x + ndc_x * visible_half_width,
    center.y - ndc_y * visible_half_height,
    world_z,
  };
}

inline Vec2 RenderAlgorithm_WorldToNdc(Vec3 world, Vec3 center, float visible_half_width, float visible_half_height) {
  return Vec2{
    (world.x - center.x) / visible_half_width,
    -(world.y - center.y) / visible_half_height,
  };
}

}  // namespace foundation

using foundation::RenderAlgorithm_BuildCenteredViewMatrix;
using foundation::RenderAlgorithm_BuildOrthoProjectionMatrix;
using foundation::RenderAlgorithm_WorldToNdc;
using foundation::RenderAlgorithm_WorldToWindow;
using foundation::RenderAlgorithm_WindowToWorld;
