#pragma once

#include "common_data/mesh.h"
#include "common_data/vector_types.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace codec {

struct AdvancedAlgorithmDebugSignal {
  std::string name;
  std::string payload;
};

struct MeshCoderOutput {
  std::vector<Vec3> vertices;
  std::vector<std::array<uint32_t, 2>> edges;
  std::vector<std::array<uint32_t, 3>> triangles;
  std::vector<float> triangle_material_gpa;
};

struct ImpactModelSource {
  std::vector<Vec3> points;
  std::vector<Vec3> velocities;
};

struct ImpactModelCoderOutput {
  std::vector<Vec3> points;
  std::vector<Vec3> velocities;
};

struct MeshCommonReflection {
  std::vector<Vec3> dot_array;
  std::vector<std::array<uint32_t, 2>> edge_array;
  std::vector<Vec3> normal_array;
  bool valid{false};
};

struct ExplicitPointReflection {
  int32_t point_id{-1};
  Vec3 point{};
  bool valid{false};
};

struct VolumeDescriptor {
  std::vector<Vec3> point_velocity;
  std::vector<Vec3> point_position;
  float mass{1.0f};
  Vec3 driving_dir{};
};

}  // namespace codec
