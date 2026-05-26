#pragma once

#include "math_types.h"

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

