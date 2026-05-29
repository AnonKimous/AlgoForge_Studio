#include "phys_manager_buffer_reflectors.h"

#include "../math/phys_algorithm/velocity_math.h"

#include <cstring>
#include <type_traits>

namespace {

struct _PackedBufferHeader {
  uint32_t entry_count{};
};

struct _PackedVelocityGuidanceEntry {
  int32_t primary_vertex{-1};
  uint32_t vertex_count{};
  Vec3 start{};
  Vec3 requested_target{};
};

struct _PackedGuideVelocityEntry {
  uint32_t vertex_count{};
  Vec3 velocity_vector{};
  Vec3 display_delta{};
  uint32_t start_frame_offset{};
  uint32_t duration_frames{};
};

struct _PackedGuideForceEntry {
  uint32_t vertex_count{};
  Vec3 force{};
  Vec3 display_delta{};
  uint32_t start_frame_offset{};
  uint32_t duration_frames{};
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

CreateDataReflectionInfo _CreateBufferReflectorInfo(const char* name) {
  CreateDataReflectionInfo info{};
  info.filled_data_formats.push_back(ReflectionDataFormat{name, 0u, 1u});
  info.algorithm_required_formats.push_back(ReflectionDataFormat{name, 0u, 1u});
  return info;
}

void _ApplyVelocityGuidanceBytes(
  const std::vector<std::byte>& data_bytes,
  void* receiver_context) {
  core_services::PhysManagerBufferDecodeContext* context =
    static_cast<core_services::PhysManagerBufferDecodeContext*>(receiver_context);
  if (!context || !context->guidances) return;

  size_t offset = 0;
  _PackedBufferHeader header{};
  if (!_ReadPod(data_bytes, &offset, &header)) return;
  for (uint32_t i = 0; i < header.entry_count; ++i) {
    _PackedVelocityGuidanceEntry packed{};
    if (!_ReadPod(data_bytes, &offset, &packed)) return;
    VelocityGuidance guidance{};
    guidance.vertex = packed.primary_vertex;
    guidance.start = packed.start;
    guidance.requested_target = packed.requested_target;
    guidance.allowed_target = packed.requested_target;
    guidance.total_velocity = MakeLinearVelocityMatrix(Vec3{
      packed.requested_target.x - packed.start.x,
      packed.requested_target.y - packed.start.y,
      packed.requested_target.z - packed.start.z,
    });
    guidance.valid = true;
    guidance.vertices.reserve(packed.vertex_count);
    for (uint32_t vertex_index = 0; vertex_index < packed.vertex_count; ++vertex_index) {
      int32_t vertex = -1;
      if (!_ReadPod(data_bytes, &offset, &vertex)) return;
      guidance.vertices.push_back(vertex);
    }
    if (!guidance.vertices.empty()) {
      guidance.vertex = guidance.vertices.front();
    }
    context->guidances->push_back(std::move(guidance));
  }
}

void _ApplyGuideVelocityBytes(
  const std::vector<std::byte>& data_bytes,
  void* receiver_context) {
  core_services::PhysManagerBufferDecodeContext* context =
    static_cast<core_services::PhysManagerBufferDecodeContext*>(receiver_context);
  if (!context || !context->guide_velocities) return;

  size_t offset = 0;
  _PackedBufferHeader header{};
  if (!_ReadPod(data_bytes, &offset, &header)) return;
  for (uint32_t i = 0; i < header.entry_count; ++i) {
    _PackedGuideVelocityEntry packed{};
    if (!_ReadPod(data_bytes, &offset, &packed)) return;
    VelocityGuideVelocity guide{};
    guide.velocity = MakeLinearVelocityMatrix(packed.velocity_vector);
    guide.display_delta = packed.display_delta;
    guide.start_frame_offset = packed.start_frame_offset;
    guide.duration_frames = packed.duration_frames;
    guide.valid = true;
    guide.vertices.reserve(packed.vertex_count);
    for (uint32_t vertex_index = 0; vertex_index < packed.vertex_count; ++vertex_index) {
      int32_t vertex = -1;
      if (!_ReadPod(data_bytes, &offset, &vertex)) return;
      guide.vertices.push_back(vertex);
    }
    context->guide_velocities->push_back(std::move(guide));
  }
}

void _ApplyGuideForceBytes(
  const std::vector<std::byte>& data_bytes,
  void* receiver_context) {
  core_services::PhysManagerBufferDecodeContext* context =
    static_cast<core_services::PhysManagerBufferDecodeContext*>(receiver_context);
  if (!context || !context->guide_forces) return;

  size_t offset = 0;
  _PackedBufferHeader header{};
  if (!_ReadPod(data_bytes, &offset, &header)) return;
  for (uint32_t i = 0; i < header.entry_count; ++i) {
    _PackedGuideForceEntry packed{};
    if (!_ReadPod(data_bytes, &offset, &packed)) return;
    VelocityGuideForce guide{};
    guide.force = packed.force;
    guide.display_delta = packed.display_delta;
    guide.start_frame_offset = packed.start_frame_offset;
    guide.duration_frames = packed.duration_frames;
    guide.valid = true;
    guide.vertices.reserve(packed.vertex_count);
    for (uint32_t vertex_index = 0; vertex_index < packed.vertex_count; ++vertex_index) {
      int32_t vertex = -1;
      if (!_ReadPod(data_bytes, &offset, &vertex)) return;
      guide.vertices.push_back(vertex);
    }
    context->guide_forces->push_back(std::move(guide));
  }
}

}  // namespace

namespace core_services {

IoDataBufferEntry CreateVelocityGuidanceReflectorBufferEntry() {
  IoDataBufferEntry entry{};
  entry.name = "velocity_guidance_reflector";
  entry.kind = IoDataBufferEntryKind::Reflector;
  entry.reflector.name = entry.name;
  entry.reflector.reflection_info = _CreateBufferReflectorInfo("velocity_guidance_buffer");
  entry.reflector.apply_callback = _ApplyVelocityGuidanceBytes;
  return entry;
}

IoDataBufferEntry CreateGuideVelocityReflectorBufferEntry() {
  IoDataBufferEntry entry{};
  entry.name = "guide_velocity_reflector";
  entry.kind = IoDataBufferEntryKind::Reflector;
  entry.reflector.name = entry.name;
  entry.reflector.reflection_info = _CreateBufferReflectorInfo("guide_velocity_buffer");
  entry.reflector.apply_callback = _ApplyGuideVelocityBytes;
  return entry;
}

IoDataBufferEntry CreateGuideForceReflectorBufferEntry() {
  IoDataBufferEntry entry{};
  entry.name = "guide_force_reflector";
  entry.kind = IoDataBufferEntryKind::Reflector;
  entry.reflector.name = entry.name;
  entry.reflector.reflection_info = _CreateBufferReflectorInfo("guide_force_buffer");
  entry.reflector.apply_callback = _ApplyGuideForceBytes;
  return entry;
}

IoDataBufferEntry CreateVelocityGuidanceDataBufferEntry(const std::vector<VelocityGuidance>& guidances) {
  IoDataBufferEntry entry{};
  entry.name = "velocity_guidance_data";
  entry.kind = IoDataBufferEntryKind::Data;
  _AppendPod(&entry.bytes, _PackedBufferHeader{static_cast<uint32_t>(guidances.size())});
  for (const VelocityGuidance& guidance : guidances) {
    _AppendPod(&entry.bytes, _PackedVelocityGuidanceEntry{
      guidance.vertex,
      static_cast<uint32_t>(guidance.vertices.size()),
      guidance.start,
      guidance.requested_target,
    });
    for (int vertex : guidance.vertices) {
      _AppendPod(&entry.bytes, static_cast<int32_t>(vertex));
    }
  }
  return entry;
}

IoDataBufferEntry CreateGuideVelocityDataBufferEntry(const std::vector<VelocityGuideVelocity>& guide_velocities) {
  IoDataBufferEntry entry{};
  entry.name = "guide_velocity_data";
  entry.kind = IoDataBufferEntryKind::Data;
  _AppendPod(&entry.bytes, _PackedBufferHeader{static_cast<uint32_t>(guide_velocities.size())});
  for (const VelocityGuideVelocity& guide : guide_velocities) {
    _AppendPod(&entry.bytes, _PackedGuideVelocityEntry{
      static_cast<uint32_t>(guide.vertices.size()),
      ExtractLinearVelocity(guide.velocity),
      guide.display_delta,
      guide.start_frame_offset,
      guide.duration_frames,
    });
    for (int vertex : guide.vertices) {
      _AppendPod(&entry.bytes, static_cast<int32_t>(vertex));
    }
  }
  return entry;
}

IoDataBufferEntry CreateGuideForceDataBufferEntry(const std::vector<VelocityGuideForce>& guide_forces) {
  IoDataBufferEntry entry{};
  entry.name = "guide_force_data";
  entry.kind = IoDataBufferEntryKind::Data;
  _AppendPod(&entry.bytes, _PackedBufferHeader{static_cast<uint32_t>(guide_forces.size())});
  for (const VelocityGuideForce& guide : guide_forces) {
    _AppendPod(&entry.bytes, _PackedGuideForceEntry{
      static_cast<uint32_t>(guide.vertices.size()),
      guide.force,
      guide.display_delta,
      guide.start_frame_offset,
      guide.duration_frames,
    });
    for (int vertex : guide.vertices) {
      _AppendPod(&entry.bytes, static_cast<int32_t>(vertex));
    }
  }
  return entry;
}

}  // namespace core_services
