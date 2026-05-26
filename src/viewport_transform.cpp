#include "viewport_transform.h"

#include <algorithm>

void ViewportTransform::SetSize(int width, int height) {
  width_ = std::max(width, 1);
  height_ = std::max(height, 1);
}

Vec2 ViewportTransform::WindowToNdc(Vec2 pixel) const {
  return Vec2{
    (pixel.x / static_cast<float>(width_)) * 2.0f - 1.0f,
    1.0f - (pixel.y / static_cast<float>(height_)) * 2.0f,
  };
}

Vec2 ViewportTransform::NdcToWindow(Vec2 ndc) const {
  return Vec2{
    (ndc.x * 0.5f + 0.5f) * static_cast<float>(width_),
    (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(height_),
  };
}

float ViewportTransform::PixelRadiusToNdcX(float pixels) const {
  return pixels * 2.0f / static_cast<float>(width_);
}

