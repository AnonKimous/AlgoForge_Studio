#pragma once

#include "mesh.h"

#include <array>
#include <cstdint>
#include <vector>

struct TriangleMatrix2D {
  uint32_t triangle_index{};
  uint32_t origin_vertex{};
  std::array<uint32_t, 2> column_vertices{};
  float m00{}, m01{};
  float m10{}, m11{};
  float determinant{};
  bool faces_viewer{};
};

class TriangleOrientationAnalyzer {
 public:
  TriangleOrientationAnalyzer() = default;
  explicit TriangleOrientationAnalyzer(const Mesh& mesh);

  void Tick(const Mesh& mesh);
  void Tick(const Mesh& current_mesh, const Mesh& reference_mesh);
  const std::vector<TriangleMatrix2D>& matrices() const { return matrices_; }
  const std::vector<bool>& triangle_faces_viewer() const { return triangle_faces_viewer_; }
  const std::vector<bool>& vertex_has_negative_triangle() const { return vertex_has_negative_triangle_; }
  const std::vector<bool>& edge_has_negative_triangle() const { return edge_has_negative_triangle_; }

 private:
  static TriangleMatrix2D BuildMatrixForTriangle(const Mesh& current_mesh, const Mesh& reference_mesh, uint32_t triangle_index);

  std::vector<TriangleMatrix2D> matrices_;
  std::vector<bool> triangle_faces_viewer_;
  std::vector<bool> vertex_has_negative_triangle_;
  std::vector<bool> edge_has_negative_triangle_;
};
