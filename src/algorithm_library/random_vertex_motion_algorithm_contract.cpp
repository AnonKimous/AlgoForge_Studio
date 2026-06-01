#include "random_vertex_motion_algorithm_contract.h"

#include <algorithm>
#include <array>

namespace algorithm_library {

AlgorithmComplianceDescriptor CreateRandomVertexMotionAlgorithmComplianceDescriptor(
  const Mesh& mesh,
  float motion_radius) {
  AlgorithmComplianceDescriptor descriptor{};
  descriptor.algorithm_name = kRandomVertexMotionAlgorithmName;
  descriptor.cpu_available = true;
  descriptor.gpu_available = false;
  descriptor.motion_radius = std::max(0.0f, motion_radius);
  descriptor.data_contract.arrays_to_allocate = {
    AlgorithmBufferRequirement{"vertex_positions", static_cast<uint32_t>(mesh.positions.size()), static_cast<uint32_t>(sizeof(Vec3))},
    AlgorithmBufferRequirement{"triangle_edges", static_cast<uint32_t>(mesh.edges.size()), static_cast<uint32_t>(sizeof(std::array<uint32_t, 2>))},
  };
  descriptor.data_contract.temporary_registers_to_allocate = {
    AlgorithmBufferRequirement{"motion_radius", 1u, static_cast<uint32_t>(sizeof(float))},
  };
  descriptor.data_contract.filled_data_formats = {
    AlgorithmDataFormat{"vertex_positions", static_cast<uint32_t>(mesh.positions.size()), static_cast<uint32_t>(sizeof(Vec3))},
    AlgorithmDataFormat{"triangle_edges", static_cast<uint32_t>(mesh.edges.size()), static_cast<uint32_t>(sizeof(std::array<uint32_t, 2>))},
  };
  descriptor.data_contract.algorithm_required_formats = {
    AlgorithmDataFormat{"mesh_vertices", static_cast<uint32_t>(mesh.positions.size()), static_cast<uint32_t>(sizeof(Vec3))},
    AlgorithmDataFormat{"mesh_edges", static_cast<uint32_t>(mesh.edges.size()), static_cast<uint32_t>(sizeof(std::array<uint32_t, 2>))},
  };
  return descriptor;
}

}  // namespace algorithm_library
