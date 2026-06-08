#pragma once

#include "codec/codec_protocol.h"
#include "common_data/interaction/interaction_signals.h"
#include "common_data/io_packet.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace codec {

inline constexpr const char* kAlgorithmInterventionIoProtocolName = "algorithm_intervention_v2";

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
