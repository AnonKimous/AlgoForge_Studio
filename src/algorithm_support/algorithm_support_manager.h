#pragma once

#include "algorithm_support/algorithm_data.h"

#include <cstddef>
#include <cstdint>

namespace algorithm_support {

class AlgorithmSupportManager {
 public:
  MeshCoderOutput BuildMeshCoderOutput(const Mesh& mesh) const;
  ImpactModelCoderOutput BuildImpactModelCoderOutput(const ImpactModelSource& source, size_t max_particles) const;
  MeshCommonReflection ReflectMeshCommon(const Mesh& mesh) const;
  ExplicitPointReflection ReflectPointById(const Mesh& mesh, int32_t point_id) const;
  VolumeDescriptor BuildVolumeDescriptorFromMesh(const Mesh& mesh, float mass, Vec3 driving_dir) const;
};

}  // namespace algorithm_support

using algorithm_support::AlgorithmSupportManager;
using algorithm_support::ImpactModelCoderOutput;
using algorithm_support::ImpactModelSource;
using algorithm_support::MeshCommonReflection;
using algorithm_support::MeshCoderOutput;
using algorithm_support::VolumeDescriptor;

namespace algorithm_management {
using algorithm_support::AlgorithmSupportManager;
using algorithm_support::ImpactModelCoderOutput;
using algorithm_support::ImpactModelSource;
using algorithm_support::MeshCommonReflection;
using algorithm_support::MeshCoderOutput;
using algorithm_support::VolumeDescriptor;
}  // namespace algorithm_management
