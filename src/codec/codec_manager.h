#pragma once

#include "common_data/common_data.h"
#include "common_data/io_packet.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace agent {
struct AgentAlgorithmCodecGroup;
class IAlgorithmIntervention;
class IAlgorithmPackageCodec;
class IAlgorithmPackageDecomposer;
}  // namespace agent

namespace codec {

inline constexpr const char* kAlgorithmInterventionIoProtocolName = "algorithm_intervention_v2";

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
  float radius{0.0f};
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

bool CreateAlgorithmPackageReflectorByName(
  const std::string& algorithm_name,
  std::shared_ptr<agent::IAlgorithmPackageCodec>* out_reflector,
  std::string* out_error_message = nullptr);

bool CreateAlgorithmPackageDecomposerByName(
  const std::string& algorithm_name,
  std::shared_ptr<agent::IAlgorithmPackageDecomposer>* out_decomposer,
  std::string* out_error_message = nullptr);

bool CreateAlgorithmInterventionByName(
  const std::string& algorithm_name,
  std::shared_ptr<agent::IAlgorithmIntervention>* out_intervention,
  std::string* out_error_message = nullptr);

bool CreateAlgorithmCodecGroupByName(
  const std::string& algorithm_name,
  agent::AgentAlgorithmCodecGroup* out_group,
  std::string* out_error_message = nullptr);

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
