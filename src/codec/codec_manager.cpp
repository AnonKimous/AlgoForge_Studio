#include "codec_manager.h"

#include <algorithm>

namespace codec {

namespace {

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

}  // namespace codec
