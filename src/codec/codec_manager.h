#pragma once

#include "codec/codec_data.h"

#include <cstddef>
#include <cstdint>

namespace codec {

class CodecManager {
 public:
  MeshCoderOutput BuildMeshCoderOutput(const Mesh& mesh) const;
  ImpactModelCoderOutput BuildImpactModelCoderOutput(const ImpactModelSource& source, size_t max_particles) const;
  MeshCommonReflection ReflectMeshCommon(const Mesh& mesh) const;
  ExplicitPointReflection ReflectPointById(const Mesh& mesh, int32_t point_id) const;
  VolumeDescriptor BuildVolumeDescriptorFromMesh(const Mesh& mesh, float mass, Vec3 driving_dir) const;
};

}  // namespace codec

using codec::CodecManager;
using codec::ImpactModelCoderOutput;
using codec::ImpactModelSource;
using codec::MeshCommonReflection;
using codec::MeshCoderOutput;
using codec::VolumeDescriptor;
