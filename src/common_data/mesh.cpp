#include "common_data/mesh.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace common_data {

namespace {

constexpr float kRigidMaterialGpa = 1000000.0f;

void AddEdge(Mesh& mesh, uint32_t a, uint32_t b) {
  if (a > b) std::swap(a, b);
  for (const auto& edge : mesh.edges) {
    if (edge[0] == a && edge[1] == b) return;
  }
  mesh.edges.push_back({a, b});
}

Vec3 Midpoint(const Vec3& a, const Vec3& b) {
  return Vec3{
    (a.x + b.x) * 0.5f,
    (a.y + b.y) * 0.5f,
    (a.z + b.z) * 0.5f,
  };
}

float DefaultMaterialValue() {
  return std::numeric_limits<float>::quiet_NaN();
}

}  // namespace

Mesh BuildDefaultTriangleMesh() {
  const std::vector<Vec3> base = {
    {-0.110f, -0.090f, 0.0f},
    {0.110f, -0.090f, 0.0f},
    {0.000f, 0.100f, 0.0f},
  };

  Mesh mesh{};
  mesh.positions = base;
  mesh.positions.push_back(Midpoint(base[0], base[1]));
  mesh.positions.push_back(Midpoint(base[1], base[2]));
  mesh.positions.push_back(Midpoint(base[2], base[0]));
  mesh.normals.assign(mesh.positions.size(), Vec3{0.0f, 0.0f, 1.0f});
  mesh.triangles = {
    {0, 3, 5},
    {3, 1, 4},
    {5, 4, 2},
    {3, 4, 5},
  };
  mesh.triangle_material_gpa.assign(mesh.triangles.size(), std::numeric_limits<float>::quiet_NaN());
  RebuildEdges(mesh);
  NormalizeTriangleMaterials(mesh);
  return mesh;
}

void NormalizeTriangleMaterials(Mesh& mesh) {
  if (mesh.triangle_material_gpa.size() != mesh.triangles.size()) {
    mesh.triangle_material_gpa.resize(mesh.triangles.size(), DefaultMaterialValue());
  }

  float sum = 0.0f;
  uint32_t count = 0;
  for (float material : mesh.triangle_material_gpa) {
    if (std::isfinite(material) && material > 0.0f) {
      sum += material;
      ++count;
    }
  }

  if (count == 0) {
    std::fill(mesh.triangle_material_gpa.begin(), mesh.triangle_material_gpa.end(), kRigidMaterialGpa);
    return;
  }

  float mean = sum / static_cast<float>(count);
  if (!(std::isfinite(mean) && mean > 0.0f)) {
    mean = kRigidMaterialGpa;
  }

  for (float& material : mesh.triangle_material_gpa) {
    if (!(std::isfinite(material) && material > 0.0f)) {
      material = mean;
    }
  }
}

void RebuildEdges(Mesh& mesh) {
  mesh.edges.clear();
  for (const auto& tri : mesh.triangles) {
    AddEdge(mesh, tri[0], tri[1]);
    AddEdge(mesh, tri[1], tri[2]);
    AddEdge(mesh, tri[2], tri[0]);
  }
}

int FindNearestVertex(const Mesh& mesh, Vec2 xy, float max_distance) {
  int nearest = -1;
  float best = max_distance * max_distance;
  for (uint32_t i = 0; i < mesh.positions.size(); ++i) {
    float dx = mesh.positions[i].x - xy.x;
    float dy = mesh.positions[i].y - xy.y;
    float dist2 = dx * dx + dy * dy;
    if (dist2 <= best) {
      best = dist2;
      nearest = static_cast<int>(i);
    }
  }
  return nearest;
}

}  // namespace common_data
