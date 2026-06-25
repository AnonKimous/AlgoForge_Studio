#pragma once

#include "common_data/interaction/interaction_signals.h"
#include "common_data/io_packet.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace algorithm_support {

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

namespace interaction_protocol_detail {

struct PackedAlgorithmInterventionEntry {
  int32_t mode{};
  float radius{};
  float velocity_magnitude{};
  uint32_t velocity_delay_frames{};
  uint32_t velocity_duration_frames{};
  float force_magnitude{};
  uint32_t force_delay_frames{};
  uint32_t force_duration_frames{};
};

template <typename T>
inline void AppendPod(std::vector<std::byte>* bytes, const T& value) {
  static_assert(std::is_trivially_copyable_v<T>);
  if (!bytes) {
    return;
  }
  const size_t offset = bytes->size();
  bytes->resize(offset + sizeof(T));
  std::memcpy(bytes->data() + offset, &value, sizeof(T));
}

template <typename T>
inline bool ReadPod(const std::vector<std::byte>& bytes, size_t* offset, T* value) {
  static_assert(std::is_trivially_copyable_v<T>);
  if (!offset || !value) {
    return false;
  }
  if (*offset + sizeof(T) > bytes.size()) {
    return false;
  }
  std::memcpy(value, bytes.data() + *offset, sizeof(T));
  *offset += sizeof(T);
  return true;
}

inline IoSignalBufferEntry BuildSignalEntry(
  const char* name,
  uint32_t data_count,
  const std::string& source_module_name,
  uint32_t source_buffer_id,
  const std::string& target_module_name,
  uint32_t target_buffer_id,
  bool lock_required) {
  IoSignalBufferEntry entry{};
  entry.name = name;
  entry.data_offset = 0u;
  entry.data_length = data_count;
  entry.source_module_name = source_module_name;
  entry.source_buffer_id = source_buffer_id;
  entry.target_module_name = target_module_name;
  entry.target_buffer_id = target_buffer_id;
  entry.lock_required = lock_required;
  return entry;
}

inline bool DecodeInterventionMode(int32_t raw_mode, AlgorithmInterventionMode* mode) {
  if (!mode) {
    return false;
  }
  switch (raw_mode) {
    case static_cast<int32_t>(AlgorithmInterventionMode::Displacement):
      *mode = AlgorithmInterventionMode::Displacement;
      return true;
    case static_cast<int32_t>(AlgorithmInterventionMode::Velocity):
      *mode = AlgorithmInterventionMode::Velocity;
      return true;
    case static_cast<int32_t>(AlgorithmInterventionMode::Force):
      *mode = AlgorithmInterventionMode::Force;
      return true;
    default:
      return false;
  }
}

inline AlgorithmInterventionDescriptor ToAlgorithmInterventionDescriptor(
  const InteractionInterventionRequest& request) {
  AlgorithmInterventionDescriptor descriptor{};
  descriptor.mode = static_cast<AlgorithmInterventionMode>(request.mode);
  descriptor.radius = request.radius;
  descriptor.velocity_magnitude = request.velocity_magnitude;
  descriptor.velocity_delay_frames = request.velocity_delay_frames;
  descriptor.velocity_duration_frames = request.velocity_duration_frames;
  descriptor.force_magnitude = request.force_magnitude;
  descriptor.force_delay_frames = request.force_delay_frames;
  descriptor.force_duration_frames = request.force_duration_frames;
  descriptor.source_module_name = request.source_module_name;
  descriptor.source_buffer_id = request.source_buffer_id;
  descriptor.target_module_name = request.target_module_name;
  descriptor.target_buffer_id = request.target_buffer_id;
  descriptor.lock_required = request.lock_required;
  return descriptor;
}

inline InteractionInterventionRequest ToInteractionInterventionRequest(
  const AlgorithmInterventionDescriptor& descriptor) {
  InteractionInterventionRequest request{};
  request.enabled = true;
  request.mode = static_cast<InteractionInterventionMode>(descriptor.mode);
  request.radius = descriptor.radius;
  request.velocity_magnitude = descriptor.velocity_magnitude;
  request.velocity_delay_frames = descriptor.velocity_delay_frames;
  request.velocity_duration_frames = descriptor.velocity_duration_frames;
  request.force_magnitude = descriptor.force_magnitude;
  request.force_delay_frames = descriptor.force_delay_frames;
  request.force_duration_frames = descriptor.force_duration_frames;
  request.source_module_name = descriptor.source_module_name;
  request.source_buffer_id = descriptor.source_buffer_id;
  request.target_module_name = descriptor.target_module_name;
  request.target_buffer_id = descriptor.target_buffer_id;
  request.lock_required = descriptor.lock_required;
  return request;
}

inline IoDataBufferEntry CreateAlgorithmInterventionDataBufferEntry(
  const AlgorithmInterventionDescriptor& descriptor) {
  IoDataBufferEntry entry{};
  entry.name = "algorithm_intervention_data";
  AppendPod(&entry.bytes, PackedAlgorithmInterventionEntry{
    static_cast<int32_t>(descriptor.mode),
    descriptor.radius,
    std::max(0.0f, descriptor.velocity_magnitude),
    descriptor.velocity_delay_frames,
    std::max(1u, descriptor.velocity_duration_frames),
    std::max(0.0f, descriptor.force_magnitude),
    descriptor.force_delay_frames,
    std::max(1u, descriptor.force_duration_frames),
  });
  return entry;
}

inline bool DecodeAlgorithmInterventionData(
  const IoDataBufferEntry& entry,
  DecodedAlgorithmIntervention* decoded) {
  if (!decoded) {
    return false;
  }

  size_t offset = 0u;
  PackedAlgorithmInterventionEntry packed{};
  if (!ReadPod(entry.bytes, &offset, &packed)) {
    return false;
  }
  if (offset != entry.bytes.size()) {
    return false;
  }

  AlgorithmInterventionMode mode = AlgorithmInterventionMode::Displacement;
  if (!DecodeInterventionMode(packed.mode, &mode)) {
    return false;
  }
  decoded->mode = mode;
  decoded->radius = std::max(0.0f, packed.radius);
  decoded->velocity_magnitude = std::max(0.0f, packed.velocity_magnitude);
  decoded->velocity_delay_frames = packed.velocity_delay_frames;
  decoded->velocity_duration_frames = std::max(1u, packed.velocity_duration_frames);
  decoded->force_magnitude = std::max(0.0f, packed.force_magnitude);
  decoded->force_delay_frames = packed.force_delay_frames;
  decoded->force_duration_frames = std::max(1u, packed.force_duration_frames);
  return true;
}

}  // namespace interaction_protocol_detail

inline IoBufferPacket BuildAlgorithmInterventionPacket(const AlgorithmInterventionDescriptor& descriptor) {
  IoBufferPacket packet{};
  packet.protocol.name = kAlgorithmInterventionIoProtocolName;

  IoDataBufferEntry entry = interaction_protocol_detail::CreateAlgorithmInterventionDataBufferEntry(descriptor);
  entry.buffer_id = descriptor.target_buffer_id;
  entry.source_buffer_id = descriptor.source_buffer_id;
  packet.data_buffer.push_back(std::move(entry));
  packet.signal_buffer.push_back(interaction_protocol_detail::BuildSignalEntry(
    "algorithm_intervention",
    1u,
    descriptor.source_module_name,
    descriptor.source_buffer_id,
    descriptor.target_module_name,
    descriptor.target_buffer_id,
    descriptor.lock_required));
  return packet;
}

inline bool DecodeAlgorithmInterventionPacket(
  const IoBufferPacket& packet,
  DecodedAlgorithmIntervention* decoded) {
  if (!decoded) {
    return false;
  }
  if (packet.protocol.name != kAlgorithmInterventionIoProtocolName) {
    return false;
  }
  if (packet.data_buffer.empty()) {
    return false;
  }

  for (const IoDataBufferEntry& entry : packet.data_buffer) {
    if (entry.name == "algorithm_intervention_data") {
      return interaction_protocol_detail::DecodeAlgorithmInterventionData(entry, decoded);
    }
  }
  return false;
}

inline IoBufferPacket BuildAlgorithmInterventionPacket(const InteractionInterventionRequest& request) {
  return BuildAlgorithmInterventionPacket(interaction_protocol_detail::ToAlgorithmInterventionDescriptor(request));
}

inline bool DecodeAlgorithmInterventionPacket(
  const IoBufferPacket& packet,
  InteractionInterventionRequest* request) {
  if (!request) {
    return false;
  }
  DecodedAlgorithmIntervention decoded{};
  if (!DecodeAlgorithmInterventionPacket(packet, &decoded)) {
    return false;
  }
  *request = interaction_protocol_detail::ToInteractionInterventionRequest(decoded);
  return true;
}

}  // namespace algorithm_support

using algorithm_support::AlgorithmInterventionDescriptor;
using algorithm_support::AlgorithmInterventionMode;
using algorithm_support::DecodedAlgorithmIntervention;

namespace algorithm_management {
using algorithm_support::AlgorithmInterventionDescriptor;
using algorithm_support::AlgorithmInterventionMode;
using algorithm_support::DecodedAlgorithmIntervention;
}  // namespace algorithm_management
