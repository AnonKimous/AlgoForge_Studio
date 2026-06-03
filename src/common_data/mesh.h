#pragma once

#include "common_data/vector_types.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace common_data {

struct Mesh {
  std::vector<Vec3> positions;
  std::vector<Vec3> normals;
  std::vector<std::array<uint32_t, 3>> triangles;
  std::vector<std::array<uint32_t, 2>> edges;
  std::vector<float> triangle_material_gpa;
};

void NormalizeTriangleMaterials(Mesh& mesh);
void RebuildEdges(Mesh& mesh);
int FindNearestVertex(const Mesh& mesh, Vec2 xy, float max_distance);

}  // namespace common_data


using common_data::FindNearestVertex;
using common_data::Mesh;
using common_data::NormalizeTriangleMaterials;
using common_data::RebuildEdges;
