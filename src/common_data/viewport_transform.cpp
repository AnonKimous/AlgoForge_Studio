#include "common_data/viewport_transform.h"

#include "common_data/render_algorithm/viewport_math.h"

#include <algorithm>

namespace common_data {

void ViewportTransform::SetSize(int width, int height) {
  width_ = std::max(width, 1);
  height_ = std::max(height, 1);
}

Vec2 ViewportTransform::WindowToNdc(Vec2 pixel) const {
  return RenderAlgorithm_WindowToNdc(pixel, width_, height_);
}

Vec2 ViewportTransform::NdcToWindow(Vec2 ndc) const {
  return RenderAlgorithm_NdcToWindow(ndc, width_, height_);
}

float ViewportTransform::PixelRadiusToNdcX(float pixels) const {
  return RenderAlgorithm_PixelRadiusToNdcX(pixels, width_);
}

}  // namespace common_data
