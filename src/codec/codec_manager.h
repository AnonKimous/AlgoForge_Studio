#pragma once

#include "common_data/mesh.h"
#include "common_data/interaction/interaction_signals.h"
#include "messaging/io_buffers.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace codec {

inline constexpr const char* kAlgorithmInterventionCodecIoProtocolName = "algorithm_intervention_codec_v2";

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

struct AdvancedAlgorithmDebugSignal {
  std::string name;
  std::string payload;
};

enum class AlgorithmInterventionMode {
  Displacement = 0,
  Velocity = 1,
  Force = 2,
};

struct AlgorithmInterventionDescriptor {
  AlgorithmInterventionMode mode{AlgorithmInterventionMode::Displacement};
  float velocity_magnitude{1.0f};
  uint32_t velocity_delay_frames{0};
  uint32_t velocity_duration_frames{1};
  float force_magnitude{1.0f};
  uint32_t force_delay_frames{0};
  uint32_t force_duration_frames{1};
  std::string source_module_name{"agent"};
  uint32_t source_buffer_id{0u};
  std::string target_module_name{"physics_agent"};
  uint32_t target_buffer_id{0u};
  bool lock_required{false};
};

using DecodedAlgorithmIntervention = AlgorithmInterventionDescriptor;

class CodecManager {
 public:
  MeshCoderOutput BuildMeshCoderOutput(const Mesh& mesh) const;
  ImpactModelCoderOutput BuildImpactModelCoderOutput(const ImpactModelSource& source, size_t max_particles) const;
  MeshCommonReflection ReflectMeshCommon(const Mesh& mesh) const;
  ExplicitPointReflection ReflectPointById(const Mesh& mesh, int32_t point_id) const;
  VolumeDescriptor BuildVolumeDescriptorFromMesh(const Mesh& mesh, float mass, Vec3 driving_dir) const;

  IoBufferPacket BuildAlgorithmInterventionPacket(const AlgorithmInterventionDescriptor& descriptor) const;
  bool DecodeAlgorithmInterventionPacket(const IoBufferPacket& packet, DecodedAlgorithmIntervention* decoded) const;
  IoBufferPacket BuildAlgorithmInterventionPacket(const InteractionInterventionRequest& request) const;
  bool DecodeAlgorithmInterventionPacket(const IoBufferPacket& packet, InteractionInterventionRequest* request) const;
};

}  // namespace codec

using codec::AlgorithmInterventionDescriptor;
using codec::AlgorithmInterventionMode;
using codec::CodecManager;
using codec::DecodedAlgorithmIntervention;
using codec::ImpactModelCoderOutput;
using codec::ImpactModelSource;
using codec::MeshCommonReflection;
using codec::MeshCoderOutput;
using codec::VolumeDescriptor;
