#include "mesh.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>

namespace {

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

}  // namespace

void GenerateSubdividedTriangleMeshFile(const std::string& path) {
  std::vector<Vec3> base = {
    {-0.55f, -0.45f, 0.0f},
    { 0.55f, -0.45f, 0.0f},
    { 0.00f,  0.50f, 0.0f},
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
  std::string tag;
  while (file >> tag) {
    if (tag == "#") {
      std::string rest;
      std::getline(file, rest);
    } else if (tag == "v") {
      Vec3 p{};
      Vec3 n{};
      file >> p.x >> p.y >> p.z >> n.x >> n.y >> n.z;
      mesh.positions.push_back(p);
      mesh.normals.push_back(n);
    } else if (tag == "t") {
      std::array<uint32_t, 3> tri{};
      file >> tri[0] >> tri[1] >> tri[2];
      mesh.triangles.push_back(tri);
    } else {
      throw std::runtime_error("Unknown mesh token: " + tag);
    }
  }

  RebuildEdges(mesh);
  return mesh;
}

Mesh LoadMeshWithSnapshot(const std::string& mesh_path, const std::string& snapshot_path, Mesh* reference_mesh) {
  Mesh base = LoadMeshFile(mesh_path);
  if (reference_mesh) {
    *reference_mesh = base;
  }

  if (!std::filesystem::exists(snapshot_path)) {
    return base;
  }

  Mesh snapshot = LoadMeshFile(snapshot_path);
  if (snapshot.positions.size() != base.positions.size()) {
    throw std::runtime_error("Snapshot vertex count does not match base mesh");
  }

  base.positions = snapshot.positions;
  return base;
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

void SaveMeshSnapshotFile(const Mesh& mesh, const std::string& path) {
  SaveMeshFile(mesh, path);
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
