#include "codec_manager.h"

#include <algorithm>
#include <cstring>
#include <type_traits>
#include <utility>

namespace codec {

namespace {

struct _PackedAlgorithmInterventionEntry {
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
void _AppendPod(std::vector<std::byte>* bytes, const T& value) {
  static_assert(std::is_trivially_copyable_v<T>);
  if (!bytes) return;
  const size_t offset = bytes->size();
  bytes->resize(offset + sizeof(T));
  std::memcpy(bytes->data() + offset, &value, sizeof(T));
}

template <typename T>
bool _ReadPod(const std::vector<std::byte>& bytes, size_t* offset, T* value) {
  static_assert(std::is_trivially_copyable_v<T>);
  if (!offset || !value) return false;
  if (*offset + sizeof(T) > bytes.size()) return false;
  std::memcpy(value, bytes.data() + *offset, sizeof(T));
  *offset += sizeof(T);
  return true;
}

IoSignalBufferEntry _BuildSignalEntry(
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

bool _DecodeInterventionMode(int32_t raw_mode, AlgorithmInterventionMode* mode) {
  if (!mode) return false;
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

AlgorithmInterventionDescriptor _ToAlgorithmInterventionDescriptor(const InteractionInterventionRequest& request) {
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

InteractionInterventionRequest _ToInteractionInterventionRequest(const AlgorithmInterventionDescriptor& descriptor) {
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

IoDataBufferEntry _CreateAlgorithmInterventionDataBufferEntry(const AlgorithmInterventionDescriptor& descriptor) {
  IoDataBufferEntry entry{};
  entry.name = "algorithm_intervention_data";
  _AppendPod(&entry.bytes, _PackedAlgorithmInterventionEntry{
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

bool _DecodeAlgorithmInterventionData(
  const IoDataBufferEntry& entry,
  DecodedAlgorithmIntervention* decoded) {
  if (!decoded) return false;

  size_t offset = 0;
  _PackedAlgorithmInterventionEntry packed{};
  if (!_ReadPod(entry.bytes, &offset, &packed)) return false;
  if (offset != entry.bytes.size()) return false;

  AlgorithmInterventionMode mode = AlgorithmInterventionMode::Displacement;
  if (!_DecodeInterventionMode(packed.mode, &mode)) return false;
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

}  // namespace

MeshCoderOutput CodecManager::BuildMeshCoderOutput(const Mesh& mesh) const {
  MeshCoderOutput result{};
  result.vertices = mesh.positions;
  result.edges = mesh.edges;
  result.triangles = mesh.triangles;
  result.triangle_material_gpa = mesh.triangle_material_gpa;
  return result;
}

ImpactModelCoderOutput CodecManager::BuildImpactModelCoderOutput(const ImpactModelSource& source, size_t max_particles) const {
  ImpactModelCoderOutput result{};
  const size_t particle_count = std::min({max_particles, source.points.size(), source.velocities.size()});
  result.points.reserve(particle_count);
  result.velocities.reserve(particle_count);
  for (size_t i = 0; i < particle_count; ++i) {
    result.points.push_back(source.points[i]);
    result.velocities.push_back(source.velocities[i]);
  }
  return result;
}

MeshCommonReflection CodecManager::ReflectMeshCommon(const Mesh& mesh) const {
  MeshCommonReflection reflected{};
  reflected.dot_array = mesh.positions;
  reflected.edge_array = mesh.edges;
  reflected.normal_array = mesh.normals;
  reflected.valid = !reflected.dot_array.empty();
  if (reflected.normal_array.size() < reflected.dot_array.size()) {
    reflected.normal_array.resize(reflected.dot_array.size(), Vec3{0.0f, 0.0f, 1.0f});
  }
  return reflected;
}

ExplicitPointReflection CodecManager::ReflectPointById(const Mesh& mesh, int32_t point_id) const {
  ExplicitPointReflection reflected{};
  reflected.point_id = point_id;
  if (point_id < 0 || static_cast<size_t>(point_id) >= mesh.positions.size()) {
    return reflected;
  }
  reflected.point = mesh.positions[static_cast<size_t>(point_id)];
  reflected.valid = true;
  return reflected;
}

VolumeDescriptor CodecManager::BuildVolumeDescriptorFromMesh(const Mesh& mesh, float mass, Vec3 driving_dir) const {
  VolumeDescriptor volume{};
  volume.point_position = mesh.positions;
  volume.point_velocity.assign(mesh.positions.size(), Vec3{0.0f, 0.0f, 0.0f});
  volume.mass = std::max(0.0001f, mass);
  volume.driving_dir = driving_dir;
  return volume;
}

IoBufferPacket CodecManager::BuildAlgorithmInterventionPacket(const AlgorithmInterventionDescriptor& descriptor) const {
  IoBufferPacket packet{};
  packet.protocol.name = kAlgorithmInterventionIoProtocolName;

  IoDataBufferEntry entry = _CreateAlgorithmInterventionDataBufferEntry(descriptor);
  entry.buffer_id = descriptor.target_buffer_id;
  entry.source_buffer_id = descriptor.source_buffer_id;
  packet.data_buffer.push_back(std::move(entry));
  packet.signal_buffer.push_back(_BuildSignalEntry(
    "algorithm_intervention",
    1u,
    descriptor.source_module_name,
    descriptor.source_buffer_id,
    descriptor.target_module_name,
    descriptor.target_buffer_id,
    descriptor.lock_required));

  return packet;
}

bool CodecManager::DecodeAlgorithmInterventionPacket(const IoBufferPacket& packet, DecodedAlgorithmIntervention* decoded) const {
  if (!decoded) return false;
  if (packet.protocol.name != kAlgorithmInterventionIoProtocolName) return false;
  if (packet.data_buffer.empty()) return false;

  for (const IoDataBufferEntry& entry : packet.data_buffer) {
    if (entry.name == "algorithm_intervention_data") {
      return _DecodeAlgorithmInterventionData(entry, decoded);
    }
  }
  return false;
}

IoBufferPacket CodecManager::BuildAlgorithmInterventionPacket(const InteractionInterventionRequest& request) const {
  return BuildAlgorithmInterventionPacket(_ToAlgorithmInterventionDescriptor(request));
}

bool CodecManager::DecodeAlgorithmInterventionPacket(const IoBufferPacket& packet, InteractionInterventionRequest* request) const {
  if (!request) return false;
  DecodedAlgorithmIntervention decoded{};
  if (!DecodeAlgorithmInterventionPacket(packet, &decoded)) {
    return false;
  }
  *request = _ToInteractionInterventionRequest(decoded);
  return true;
}

}  // namespace codec
