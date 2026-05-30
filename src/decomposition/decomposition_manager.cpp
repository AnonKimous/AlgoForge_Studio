#include "decomposition_manager.h"

#include "data_protocol/interaction/interaction_state.h"
#include "data_protocol/physics/physics_types.h"
#include "foundation/phys_algorithm/velocity_math.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <type_traits>
#include <utility>

namespace decomposition {

namespace {

bool _IsMoreLowerLeft(const Vec3& candidate, const Vec3& current) {
  float candidate_score = candidate.x + candidate.y;
  float current_score = current.x + current.y;
  if (candidate_score != current_score) return candidate_score < current_score;
  if (candidate.y != current.y) return candidate.y < current.y;
  return candidate.x < current.x;
}

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

IoSignalBufferEntry _BuildGuideSignalEntry(uint32_t data_count, const GuideUiPhysDescriptor& descriptor) {
  IoSignalBufferEntry entry{};
  entry.name = "guide_ui_phys_decomposition";
  entry.data_offset = 0u;
  entry.data_length = data_count;
  entry.source_module_name = descriptor.source_module_name;
  entry.source_buffer_id = descriptor.source_buffer_id;
  entry.target_module_name = descriptor.target_module_name;
  entry.target_buffer_id = descriptor.target_buffer_id;
  entry.lock_required = descriptor.lock_required;
  return entry;
}

IoDataBufferEntry _CreateVelocityGuidanceDataBufferEntry(const std::vector<VelocityGuidance>& guidances) {
  IoDataBufferEntry entry{};
  entry.name = "velocity_guidance_data";
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

IoDataBufferEntry _CreateGuideVelocityDataBufferEntry(const std::vector<VelocityGuideVelocity>& guide_velocities) {
  IoDataBufferEntry entry{};
  entry.name = "guide_velocity_data";
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

IoDataBufferEntry _CreateGuideForceDataBufferEntry(const std::vector<VelocityGuideForce>& guide_forces) {
  IoDataBufferEntry entry{};
  entry.name = "guide_force_data";
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

bool _DecodeVelocityGuidanceData(
  const IoDataBufferEntry& entry,
  std::vector<VelocityGuidance>* guidances) {
  if (!guidances) return false;

  size_t offset = 0;
  _PackedBufferHeader header{};
  if (!_ReadPod(entry.bytes, &offset, &header)) return false;

  guidances->reserve(guidances->size() + header.entry_count);
  for (uint32_t i = 0; i < header.entry_count; ++i) {
    _PackedVelocityGuidanceEntry packed{};
    if (!_ReadPod(entry.bytes, &offset, &packed)) return false;

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
      if (!_ReadPod(entry.bytes, &offset, &vertex)) return false;
      guidance.vertices.push_back(vertex);
    }
    if (!guidance.vertices.empty()) {
      guidance.vertex = guidance.vertices.front();
    }
    guidances->push_back(std::move(guidance));
  }

  return true;
}

bool _DecodeGuideVelocityData(
  const IoDataBufferEntry& entry,
  std::vector<VelocityGuideVelocity>* guide_velocities) {
  if (!guide_velocities) return false;

  size_t offset = 0;
  _PackedBufferHeader header{};
  if (!_ReadPod(entry.bytes, &offset, &header)) return false;

  guide_velocities->reserve(guide_velocities->size() + header.entry_count);
  for (uint32_t i = 0; i < header.entry_count; ++i) {
    _PackedGuideVelocityEntry packed{};
    if (!_ReadPod(entry.bytes, &offset, &packed)) return false;

    VelocityGuideVelocity guide{};
    guide.velocity = MakeLinearVelocityMatrix(packed.velocity_vector);
    guide.display_delta = packed.display_delta;
    guide.start_frame_offset = packed.start_frame_offset;
    guide.duration_frames = packed.duration_frames;
    guide.valid = true;
    guide.vertices.reserve(packed.vertex_count);
    for (uint32_t vertex_index = 0; vertex_index < packed.vertex_count; ++vertex_index) {
      int32_t vertex = -1;
      if (!_ReadPod(entry.bytes, &offset, &vertex)) return false;
      guide.vertices.push_back(vertex);
    }
    guide_velocities->push_back(std::move(guide));
  }

  return true;
}

bool _DecodeGuideForceData(
  const IoDataBufferEntry& entry,
  std::vector<VelocityGuideForce>* guide_forces) {
  if (!guide_forces) return false;

  size_t offset = 0;
  _PackedBufferHeader header{};
  if (!_ReadPod(entry.bytes, &offset, &header)) return false;

  guide_forces->reserve(guide_forces->size() + header.entry_count);
  for (uint32_t i = 0; i < header.entry_count; ++i) {
    _PackedGuideForceEntry packed{};
    if (!_ReadPod(entry.bytes, &offset, &packed)) return false;

    VelocityGuideForce guide{};
    guide.force = packed.force;
    guide.display_delta = packed.display_delta;
    guide.start_frame_offset = packed.start_frame_offset;
    guide.duration_frames = packed.duration_frames;
    guide.valid = true;
    guide.vertices.reserve(packed.vertex_count);
    for (uint32_t vertex_index = 0; vertex_index < packed.vertex_count; ++vertex_index) {
      int32_t vertex = -1;
      if (!_ReadPod(entry.bytes, &offset, &vertex)) return false;
      guide.vertices.push_back(vertex);
    }
    guide_forces->push_back(std::move(guide));
  }

  return true;
}

}  // namespace

MeshDecompositionResult CodecManager::SplitMesh(const Mesh& mesh) const {
  MeshDecompositionResult result{};
  result.vertices = mesh.positions;
  result.edges = mesh.edges;
  result.triangles = mesh.triangles;
  result.triangle_material_gpa = mesh.triangle_material_gpa;
  return result;
}

ImpactModelDecompositionResult CodecManager::SplitImpactModel(const ImpactModelSource& source, size_t max_particles) const {
  ImpactModelDecompositionResult result{};
  const size_t particle_count = std::min({max_particles, source.points.size(), source.velocities.size()});
  result.points.reserve(particle_count);
  result.velocities.reserve(particle_count);
  for (size_t i = 0; i < particle_count; ++i) {
    result.points.push_back(source.points[i]);
    result.velocities.push_back(source.velocities[i]);
  }
  return result;
}

TriangleOrientationCpuResourcePack CodecManager::BuildTriangleOrientationCpuResourcePack(const Mesh& current_mesh, const Mesh& reference_mesh) const {
  TriangleOrientationCpuResourcePack pack{};
  if (current_mesh.positions.size() != reference_mesh.positions.size()) {
    return pack;
  }
  if (current_mesh.triangles.size() != reference_mesh.triangles.size()) {
    return pack;
  }
  if (current_mesh.edges.size() != reference_mesh.edges.size()) {
    return pack;
  }

  pack.current_positions.reserve(current_mesh.positions.size());
  pack.reference_positions.reserve(reference_mesh.positions.size());
  for (const Vec3& position : current_mesh.positions) {
    pack.current_positions.push_back(position);
  }
  for (const Vec3& position : reference_mesh.positions) {
    pack.reference_positions.push_back(position);
  }
  pack.triangle_indices.reserve(current_mesh.triangles.size());
  for (const auto& tri : current_mesh.triangles) {
    pack.triangle_indices.push_back(tri);
  }
  pack.edge_indices.reserve(current_mesh.edges.size());
  for (const auto& edge : current_mesh.edges) {
    pack.edge_indices.push_back(edge);
  }

  pack.triangle_matrices.reserve(current_mesh.triangles.size());
  for (uint32_t tri_index = 0; tri_index < current_mesh.triangles.size(); ++tri_index) {
    const auto& tri = reference_mesh.triangles[tri_index];
    uint32_t origin_slot = 0;
    for (uint32_t slot = 1; slot < 3; ++slot) {
      if (_IsMoreLowerLeft(reference_mesh.positions[tri[slot]], reference_mesh.positions[tri[origin_slot]])) {
        origin_slot = slot;
      }
    }

    uint32_t origin = tri[origin_slot];
    uint32_t first = tri[(origin_slot + 1) % 3];
    uint32_t second = tri[(origin_slot + 2) % 3];
    const Vec3& o = current_mesh.positions[origin];
    const Vec3& a = current_mesh.positions[first];
    const Vec3& b = current_mesh.positions[second];

    TriangleMatrix2D matrix{};
    matrix.triangle_index = tri_index;
    matrix.origin_vertex = origin;
    matrix.column_vertices = {first, second};
    matrix.m00 = a.x - o.x;
    matrix.m10 = a.y - o.y;
    matrix.m01 = b.x - o.x;
    matrix.m11 = b.y - o.y;
    matrix.determinant = matrix.m00 * matrix.m11 - matrix.m01 * matrix.m10;
    matrix.faces_viewer = matrix.determinant > 0.0f ? 1u : 0u;
    pack.triangle_matrices.push_back(matrix);
  }

  pack.valid = true;
  return pack;
}

IoBufferPacket CodecManager::BuildGuideUiPhysPacket(const GuideUiPhysDescriptor& descriptor) const {
  IoBufferPacket packet{};
  packet.protocol.name = kGuideUiPhysDecompositionIoProtocolName;

  if (!descriptor.mesh || descriptor.mesh->positions.empty()) {
    return packet;
  }

  const GuideUiFrame& guide_ui_frame = descriptor.guide_ui_frame;
  if (!guide_ui_frame.dragging || guide_ui_frame.selected_vertices.empty()) {
    return packet;
  }

  const int primary_vertex = guide_ui_frame.selected_vertices.front();
  if (primary_vertex < 0 || static_cast<size_t>(primary_vertex) >= descriptor.mesh->positions.size()) {
    return packet;
  }

  const Vec3& primary_start = descriptor.mesh->positions[primary_vertex];
  Vec3 drag_delta{
    guide_ui_frame.drag_target.x - primary_start.x,
    guide_ui_frame.drag_target.y - primary_start.y,
    guide_ui_frame.drag_target.z - primary_start.z,
  };
  float drag_length = std::sqrt(
    drag_delta.x * drag_delta.x +
    drag_delta.y * drag_delta.y +
    drag_delta.z * drag_delta.z);
  Vec3 drag_direction{};
  if (drag_length > 1e-6f) {
    drag_direction = Vec3{
      drag_delta.x / drag_length,
      drag_delta.y / drag_length,
      drag_delta.z / drag_length,
    };
  }

  switch (descriptor.guide_edit_mode) {
    case GuideEditMode::Displacement: {
      VelocityGuidance guidance{};
      guidance.vertex = primary_vertex;
      guidance.vertices = guide_ui_frame.selected_vertices;
      guidance.start = primary_start;
      guidance.requested_target = Vec3{
        guide_ui_frame.drag_target.x,
        guide_ui_frame.drag_target.y,
        primary_start.z,
      };
      guidance.allowed_target = guidance.requested_target;
      guidance.total_velocity = MakeLinearVelocityMatrix(Vec3{
        guidance.requested_target.x - guidance.start.x,
        guidance.requested_target.y - guidance.start.y,
        guidance.requested_target.z - guidance.start.z,
      });
      guidance.valid = true;
      IoDataBufferEntry entry = _CreateVelocityGuidanceDataBufferEntry({guidance});
      entry.buffer_id = descriptor.target_buffer_id;
      entry.source_buffer_id = descriptor.source_buffer_id;
      packet.data_buffer.push_back(std::move(entry));
      break;
    }
    case GuideEditMode::Velocity: {
      VelocityGuideVelocity guide{};
      guide.vertices = guide_ui_frame.selected_vertices;
      guide.velocity = MakeLinearVelocityMatrix(Vec3{
        drag_direction.x * descriptor.guide_velocity_magnitude,
        drag_direction.y * descriptor.guide_velocity_magnitude,
        drag_direction.z * descriptor.guide_velocity_magnitude,
      });
      guide.display_delta = drag_delta;
      guide.start_frame_offset = descriptor.guide_velocity_delay_frames;
      guide.duration_frames = descriptor.guide_velocity_duration_frames;
      guide.valid = true;
      IoDataBufferEntry entry = _CreateGuideVelocityDataBufferEntry({guide});
      entry.buffer_id = descriptor.target_buffer_id;
      entry.source_buffer_id = descriptor.source_buffer_id;
      packet.data_buffer.push_back(std::move(entry));
      break;
    }
    case GuideEditMode::Force: {
      VelocityGuideForce guide{};
      guide.vertices = guide_ui_frame.selected_vertices;
      guide.force = Vec3{
        drag_direction.x * descriptor.guide_force_magnitude,
        drag_direction.y * descriptor.guide_force_magnitude,
        drag_direction.z * descriptor.guide_force_magnitude,
      };
      guide.display_delta = drag_delta;
      guide.start_frame_offset = descriptor.guide_force_delay_frames;
      guide.duration_frames = descriptor.guide_force_duration_frames;
      guide.valid = true;
      IoDataBufferEntry entry = _CreateGuideForceDataBufferEntry({guide});
      entry.buffer_id = descriptor.target_buffer_id;
      entry.source_buffer_id = descriptor.source_buffer_id;
      packet.data_buffer.push_back(std::move(entry));
      break;
    }
  }

  if (!packet.data_buffer.empty()) {
    packet.signal_buffer.push_back(_BuildGuideSignalEntry(static_cast<uint32_t>(packet.data_buffer.size()), descriptor));
  }

  return packet;
}

bool CodecManager::DecodeGuideUiPhysPacket(const IoBufferPacket& packet, DecomposedGuideBuffers* decoded) const {
  if (!decoded) return false;
  if (packet.protocol.name != kGuideUiPhysDecompositionIoProtocolName) return false;
  if (packet.data_buffer.empty()) return false;

  decoded->guidances.clear();
  decoded->guide_velocities.clear();
  decoded->guide_forces.clear();

  bool any_decoded = false;
  for (const IoDataBufferEntry& entry : packet.data_buffer) {
    if (entry.name == "velocity_guidance_data") {
      any_decoded = _DecodeVelocityGuidanceData(entry, &decoded->guidances) || any_decoded;
    } else if (entry.name == "guide_velocity_data") {
      any_decoded = _DecodeGuideVelocityData(entry, &decoded->guide_velocities) || any_decoded;
    } else if (entry.name == "guide_force_data") {
      any_decoded = _DecodeGuideForceData(entry, &decoded->guide_forces) || any_decoded;
    }
  }

  return any_decoded;
}

}  // namespace decomposition
