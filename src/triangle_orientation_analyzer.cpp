#include "triangle_orientation_analyzer.h"

#include <array>

namespace {

bool SameUndirectedEdge(const std::array<uint32_t, 2>& edge, uint32_t a, uint32_t b) {
  return (edge[0] == a && edge[1] == b) || (edge[0] == b && edge[1] == a);
}

bool IsMoreLowerLeft(const Vec3& candidate, const Vec3& current) {
  float candidate_score = candidate.x + candidate.y;
  float current_score = current.x + current.y;
  if (candidate_score != current_score) return candidate_score < current_score;
  if (candidate.y != current.y) return candidate.y < current.y;
  return candidate.x < current.x;
}

}  // namespace

TriangleOrientationAnalyzer::TriangleOrientationAnalyzer(const Mesh& mesh) {
  Tick(mesh);
}

void TriangleOrientationAnalyzer::Tick(const Mesh& mesh) {
  Tick(mesh, mesh);
}

void TriangleOrientationAnalyzer::Tick(const Mesh& mesh, const Mesh& reference_mesh) {
  matrices_.clear();
  matrices_.reserve(mesh.triangles.size());
  triangle_faces_viewer_.assign(mesh.triangles.size(), true);
  vertex_has_negative_triangle_.assign(mesh.positions.size(), false);
  edge_has_negative_triangle_.assign(mesh.edges.size(), false);

  for (uint32_t tri_index = 0; tri_index < mesh.triangles.size(); ++tri_index) {
    TriangleMatrix2D matrix = BuildMatrixForTriangle(mesh, reference_mesh, tri_index);
    matrices_.push_back(matrix);
    triangle_faces_viewer_[tri_index] = matrix.faces_viewer;

    if (!matrix.faces_viewer) {
      const auto& tri = mesh.triangles[tri_index];
      for (uint32_t vertex : tri) {
        vertex_has_negative_triangle_[vertex] = true;
      }

      for (uint32_t edge_index = 0; edge_index < mesh.edges.size(); ++edge_index) {
        const auto& edge = mesh.edges[edge_index];
        if (SameUndirectedEdge(edge, tri[0], tri[1]) ||
            SameUndirectedEdge(edge, tri[1], tri[2]) ||
            SameUndirectedEdge(edge, tri[2], tri[0])) {
          edge_has_negative_triangle_[edge_index] = true;
        }
      }
    }
  }
}

TriangleMatrix2D TriangleOrientationAnalyzer::BuildMatrixForTriangle(const Mesh& mesh, const Mesh& reference_mesh, uint32_t triangle_index) {
  const auto& tri = reference_mesh.triangles[triangle_index];

  uint32_t origin_slot = 0;
  for (uint32_t slot = 1; slot < 3; ++slot) {
    if (IsMoreLowerLeft(reference_mesh.positions[tri[slot]], reference_mesh.positions[tri[origin_slot]])) {
      origin_slot = slot;
    }
  }

  uint32_t origin = tri[origin_slot];
  uint32_t first = tri[(origin_slot + 1) % 3];
  uint32_t second = tri[(origin_slot + 2) % 3];

  const Vec3& o = mesh.positions[origin];
  const Vec3& a = mesh.positions[first];
  const Vec3& b = mesh.positions[second];

  TriangleMatrix2D result{};
  result.triangle_index = triangle_index;
  result.origin_vertex = origin;
  result.column_vertices = {first, second};
  result.m00 = a.x - o.x;
  result.m10 = a.y - o.y;
  result.m01 = b.x - o.x;
  result.m11 = b.y - o.y;
  result.determinant = result.m00 * result.m11 - result.m01 * result.m10;
  result.faces_viewer = result.determinant > 0.0f;
  return result;
}
