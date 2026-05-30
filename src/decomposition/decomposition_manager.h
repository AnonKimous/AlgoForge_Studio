#pragma once

#include "data_protocol/interaction/interaction_state.h"
#include "data_protocol/mesh.h"
#include "data_protocol/triangle_orientation_state.h"
#include "messaging/io_buffers.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace decomposition {

inline constexpr const char* kGuideUiPhysDecompositionIoProtocolName = "guide_ui_phys_decomposition_v1";

struct MeshDecompositionResult {
  std::vector<Vec3> vertices;
  std::vector<std::array<uint32_t, 2>> edges;
  std::vector<std::array<uint32_t, 3>> triangles;
  std::vector<float> triangle_material_gpa;
};

struct ImpactModelSource {
  std::vector<Vec3> points;
  std::vector<Vec3> velocities;
};

struct ImpactModelDecompositionResult {
  std::vector<Vec3> points;
  std::vector<Vec3> velocities;
};

struct DecomposedGuideBuffers {
  std::vector<VelocityGuidance> guidances;
  std::vector<VelocityGuideVelocity> guide_velocities;
  std::vector<VelocityGuideForce> guide_forces;
};

struct TriangleOrientationCpuResourcePack {
  std::vector<Vec3> current_positions;
  std::vector<Vec3> reference_positions;
  std::vector<std::array<uint32_t, 3>> triangle_indices;
  std::vector<std::array<uint32_t, 2>> edge_indices;
  std::vector<TriangleMatrix2D> triangle_matrices;
  bool valid{false};
};

struct GuideUiPhysDescriptor {
  const Mesh* mesh{};
  GuideUiFrame guide_ui_frame{};
  GuideEditMode guide_edit_mode{GuideEditMode::Displacement};
  float guide_velocity_magnitude{1.0f};
  uint32_t guide_velocity_delay_frames{0};
  uint32_t guide_velocity_duration_frames{1};
  float guide_force_magnitude{1.0f};
  uint32_t guide_force_delay_frames{0};
  uint32_t guide_force_duration_frames{1};
  std::string source_module_name{"guide_ui_agent"};
  uint32_t source_buffer_id{0u};
  std::string target_module_name{"phys_agent"};
  uint32_t target_buffer_id{0u};
  bool lock_required{false};
};

class CodecManager {
 public:
  MeshDecompositionResult SplitMesh(const Mesh& mesh) const;
  ImpactModelDecompositionResult SplitImpactModel(const ImpactModelSource& source, size_t max_particles) const;
  TriangleOrientationCpuResourcePack BuildTriangleOrientationCpuResourcePack(const Mesh& current_mesh, const Mesh& reference_mesh) const;

  IoBufferPacket BuildGuideUiPhysPacket(const GuideUiPhysDescriptor& descriptor) const;

  bool DecodeGuideUiPhysPacket(const IoBufferPacket& packet, DecomposedGuideBuffers* decoded) const;
};

using DecompositionManager = CodecManager;

}  // namespace decomposition

using decomposition::DecomposedGuideBuffers;
using decomposition::CodecManager;
using decomposition::DecompositionManager;
using decomposition::ImpactModelDecompositionResult;
using decomposition::ImpactModelSource;
using decomposition::MeshDecompositionResult;
using decomposition::GuideUiPhysDescriptor;
using decomposition::TriangleOrientationCpuResourcePack;
