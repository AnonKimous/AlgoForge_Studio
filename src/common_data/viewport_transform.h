#pragma once

#include "common_data/vector_types.h"

namespace common_data {

class ViewportTransform {
 public:
  void SetSize(int width, int height);
  int width() const { return width_; }
  int height() const { return height_; }

  Vec2 WindowToNdc(Vec2 pixel) const;
  Vec2 NdcToWindow(Vec2 ndc) const;
  float PixelRadiusToNdcX(float pixels) const;

 private:
  int width_{1};
  int height_{1};
};

}  // namespace common_data

using common_data::ViewportTransform;
