#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace foundation {

struct TriangleMatrix2D {
  uint32_t triangle_index{};
  uint32_t origin_vertex{};
  std::array<uint32_t, 2> column_vertices{};
  float m00{}, m01{};
  float m10{}, m11{};
  float determinant{};
  bool faces_viewer{};
};

struct TriangleOrientationState {
  std::vector<TriangleMatrix2D> matrices;
  std::vector<bool> triangle_faces_viewer;
  std::vector<bool> vertex_has_negative_triangle;
  std::vector<bool> edge_has_negative_triangle;
};

}  // namespace foundation

namespace data_protocol = foundation;

using foundation::TriangleMatrix2D;
using foundation::TriangleOrientationState;
