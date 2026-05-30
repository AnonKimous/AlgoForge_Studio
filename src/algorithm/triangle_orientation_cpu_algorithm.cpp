#include "triangle_orientation_cpu_algorithm.h"

#include "cpu_job_scheduler.h"

#include <array>

namespace algorithm {

namespace {

bool _SameUndirectedEdge(const std::array<uint32_t, 2>& edge, uint32_t a, uint32_t b) {
  return (edge[0] == a && edge[1] == b) || (edge[0] == b && edge[1] == a);
}

bool _IsMoreLowerLeft(const Vec3& candidate, const Vec3& current) {
  float candidate_score = candidate.x + candidate.y;
  float current_score = current.x + current.y;
  if (candidate_score != current_score) return candidate_score < current_score;
  if (candidate.y != current.y) return candidate.y < current.y;
  return candidate.x < current.x;
}

TriangleMatrix2D _BuildMatrixForTriangle(const Mesh& current_mesh, const Mesh& reference_mesh, uint32_t triangle_index) {
  const auto& tri = reference_mesh.triangles[triangle_index];

  uint32_t origin_slot = 0;
  for (uint32_t slot = 1; slot < 3; ++slot) {
    if (_IsMoreLowerLeft(reference_mesh.positions[tri[slot]], reference_mesh.positions[tri[origin_slot]])) {
      origin_slot = slot;
    }
  }

  uint32_t origin = tri[origin_slot];
  uint32_t first = tri[(origin_slot + 1) % 3];
  uint32_t second = tri[(origin_slot + 2) % 3];

  const Vec3& o = current_mesh.positions[origin];
  const Vec3& a = current_mesh.positions[first];
  const Vec3& b = current_mesh.positions[second];

  TriangleMatrix2D matrix{};
  matrix.triangle_index = triangle_index;
  matrix.origin_vertex = origin;
  matrix.column_vertices = {first, second};
  matrix.m00 = a.x - o.x;
  matrix.m10 = a.y - o.y;
  matrix.m01 = b.x - o.x;
  matrix.m11 = b.y - o.y;
  matrix.determinant = matrix.m00 * matrix.m11 - matrix.m01 * matrix.m10;
  matrix.faces_viewer = matrix.determinant > 0.0f;
  return matrix;
}

}  // namespace

bool RunTriangleOrientationCpuAlgorithm(
  const Mesh& current_mesh,
  const Mesh& reference_mesh,
  TriangleOrientationState* result) {
  if (!result) return false;
  if (current_mesh.triangles.size() != reference_mesh.triangles.size()) {
    return false;
  }
  if (current_mesh.positions.size() != reference_mesh.positions.size()) {
    return false;
  }

  result->matrices.clear();
  result->matrices.resize(current_mesh.triangles.size());
  result->triangle_faces_viewer.assign(current_mesh.triangles.size(), true);
  result->vertex_has_negative_triangle.assign(current_mesh.positions.size(), false);
  result->edge_has_negative_triangle.assign(current_mesh.edges.size(), false);

  RunCpuJobsOnMainThread(static_cast<uint32_t>(current_mesh.triangles.size()), [&](CpuJobRange range) {
    for (uint32_t tri_index = range.begin; tri_index < range.end; ++tri_index) {
      TriangleMatrix2D matrix = _BuildMatrixForTriangle(current_mesh, reference_mesh, tri_index);
      result->matrices[tri_index] = matrix;
      result->triangle_faces_viewer[tri_index] = matrix.faces_viewer;
    }
  });

  for (uint32_t tri_index = 0; tri_index < current_mesh.triangles.size(); ++tri_index) {
    if (result->triangle_faces_viewer[tri_index]) {
      continue;
    }

    const auto& tri = current_mesh.triangles[tri_index];
    for (uint32_t vertex : tri) {
      result->vertex_has_negative_triangle[vertex] = true;
    }

    for (uint32_t edge_index = 0; edge_index < current_mesh.edges.size(); ++edge_index) {
      const auto& edge = current_mesh.edges[edge_index];
      if (_SameUndirectedEdge(edge, tri[0], tri[1]) ||
          _SameUndirectedEdge(edge, tri[1], tri[2]) ||
          _SameUndirectedEdge(edge, tri[2], tri[0])) {
        result->edge_has_negative_triangle[edge_index] = true;
      }
    }
  }

  return true;
}

}  // namespace algorithm
