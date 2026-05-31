#include "common_data/mesh.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace common_data {

namespace {

constexpr float kRigidMaterialGpa = 1000000.0f;

Vec3 Midpoint(const Vec3& a, const Vec3& b) {
  return Vec3{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, (a.z + b.z) * 0.5f};
}

void AddEdge(Mesh& mesh, uint32_t a, uint32_t b) {
  if (a > b) std::swap(a, b);
  for (const auto& edge : mesh.edges) {
    if (edge[0] == a && edge[1] == b) return;
  }
  mesh.edges.push_back({a, b});
}

float DefaultMaterialValue() {
  return std::numeric_limits<float>::quiet_NaN();
}

}  // namespace

void GenerateSubdividedTriangleMeshFile(const std::string& path) {
  std::vector<Vec3> base = {
    {-0.110f, -0.090f, 0.0f},
    { 0.110f, -0.090f, 0.0f},
    { 0.000f,  0.100f, 0.0f},
  };

  std::vector<Vec3> vertices = base;
  vertices.push_back(Midpoint(base[0], base[1]));
  vertices.push_back(Midpoint(base[1], base[2]));
  vertices.push_back(Midpoint(base[2], base[0]));

  std::vector<std::array<uint32_t, 3>> triangles = {
    {0, 3, 5},
    {3, 1, 4},
    {5, 4, 2},
    {3, 4, 5},
  };

  std::ofstream file(path);
  if (!file) {
    throw std::runtime_error("Failed to write mesh file: " + path);
  }

  file << "# one midpoint/Loop-style subdivision of a CCW triangle\n";
  for (const auto& v : vertices) {
    file << "v " << v.x << " " << v.y << " " << v.z << " 0 0 1\n";
  }
  for (const auto& tri : triangles) {
    file << "t " << tri[0] << " " << tri[1] << " " << tri[2] << "\n";
  }
}

Mesh LoadMeshFile(const std::string& path) {
  std::ifstream file(path);
  if (!file) {
    throw std::runtime_error("Failed to read mesh file: " + path);
  }

  Mesh mesh;
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty()) continue;
    std::istringstream iss(line);
    std::string tag;
    iss >> tag;
    if (tag.empty() || tag == "#") {
      continue;
    }
    if (tag == "v") {
      Vec3 p{};
      Vec3 n{};
      iss >> p.x >> p.y >> p.z >> n.x >> n.y >> n.z;
      if (!iss) throw std::runtime_error("Malformed vertex line in mesh file: " + path);
      mesh.positions.push_back(p);
      mesh.normals.push_back(n);
    } else if (tag == "t") {
      std::array<uint32_t, 3> tri{};
      iss >> tri[0] >> tri[1] >> tri[2];
      if (!iss) throw std::runtime_error("Malformed triangle line in mesh file: " + path);
      mesh.triangles.push_back(tri);
      float material = DefaultMaterialValue();
      if (iss >> material) {
        mesh.triangle_material_gpa.push_back(material);
      } else {
        mesh.triangle_material_gpa.push_back(DefaultMaterialValue());
      }
    } else {
      throw std::runtime_error("Unknown mesh token: " + tag);
    }
  }

  RebuildEdges(mesh);
  NormalizeTriangleMaterials(mesh);
  return mesh;
}

void SaveMeshFile(const Mesh& mesh, const std::string& path) {
  std::ofstream file(path);
  if (!file) {
    throw std::runtime_error("Failed to write mesh file: " + path);
  }

  file << "# saved mesh\n";
  for (uint32_t i = 0; i < mesh.positions.size(); ++i) {
    Vec3 n = i < mesh.normals.size() ? mesh.normals[i] : Vec3{0.0f, 0.0f, 1.0f};
    const Vec3& p = mesh.positions[i];
    file << "v " << p.x << " " << p.y << " " << p.z << " " << n.x << " " << n.y << " " << n.z << "\n";
  }
  for (const auto& tri : mesh.triangles) {
    file << "t " << tri[0] << " " << tri[1] << " " << tri[2] << "\n";
  }
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
