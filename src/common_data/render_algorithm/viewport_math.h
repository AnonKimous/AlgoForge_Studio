#pragma once

#include "common_data/vector_types.h"

namespace common_data {

inline Vec2 RenderAlgorithm_WindowToNdc(Vec2 pixel, int width, int height) {
  return Vec2{
    (pixel.x / static_cast<float>(width)) * 2.0f - 1.0f,
    1.0f - (pixel.y / static_cast<float>(height)) * 2.0f,
  };
}

inline Vec2 RenderAlgorithm_NdcToWindow(Vec2 ndc, int width, int height) {
  return Vec2{
    (ndc.x * 0.5f + 0.5f) * static_cast<float>(width),
    (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(height),
  };
}

inline float RenderAlgorithm_PixelRadiusToNdcX(float pixels, int width) {
  return pixels * 2.0f / static_cast<float>(width);
}

}  // namespace common_data

using common_data::RenderAlgorithm_NdcToWindow;
using common_data::RenderAlgorithm_PixelRadiusToNdcX;
using common_data::RenderAlgorithm_WindowToNdc;
