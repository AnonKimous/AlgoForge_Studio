#pragma once

#include "common_data/interaction/interaction_signals.h"
#include "common_data/io_packet.h"

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

IoBufferPacket BuildAlgorithmInterventionPacket(const AlgorithmInterventionDescriptor& descriptor);
bool DecodeAlgorithmInterventionPacket(const IoBufferPacket& packet, DecodedAlgorithmIntervention* decoded);
IoBufferPacket BuildAlgorithmInterventionPacket(const InteractionInterventionRequest& request);
bool DecodeAlgorithmInterventionPacket(const IoBufferPacket& packet, InteractionInterventionRequest* request);

}  // namespace codec

using codec::AlgorithmInterventionDescriptor;
using codec::AlgorithmInterventionMode;
using codec::DecodedAlgorithmIntervention;
