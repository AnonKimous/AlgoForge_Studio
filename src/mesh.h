#pragma once

#include "math_types.h"

#include <array>
#include <string>
#include <vector>

struct Mesh {
  std::vector<Vec3> positions;
  std::vector<Vec3> normals;
  std::vector<std::array<uint32_t, 3>> triangles;
  std::vector<std::array<uint32_t, 2>> edges;
  std::vector<float> triangle_material_gpa;
};

void GenerateSubdividedTriangleMeshFile(const std::string& path);
Mesh LoadMeshFile(const std::string& path);
Mesh LoadMeshWithSnapshot(const std::string& mesh_path, const std::string& snapshot_path, Mesh* reference_mesh);
void SaveMeshFile(const Mesh& mesh, const std::string& path);
void SaveMeshSnapshotFile(const Mesh& mesh, const std::string& path);
void NormalizeTriangleMaterials(Mesh& mesh);
void RebuildEdges(Mesh& mesh);
int FindNearestVertex(const Mesh& mesh, Vec2 xy, float max_distance);
