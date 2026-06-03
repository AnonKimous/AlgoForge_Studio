#pragma once

#include "algorithm_package.h"
#include "common_data/vector_types.h"

#include <algorithm>
#include <string>

namespace algorithm_library {

inline constexpr const char* kCameraAlgorithmName = "camera";

class CameraAlgorithmPackageCodec
  : public algorithm::ISimpleAlgorithmPackageCodec
  , public algorithm::IAlgorithmPackageDecomposer {
 public:
  bool BuildContainerDescriptor(
    const codec::VolumeDescriptor& volume,
    AlgorithmContainerDescriptor* out_descriptor) const override {
    if (!out_descriptor) return false;

    const uint32_t point_count = static_cast<uint32_t>(std::max<size_t>(1u, volume.point_position.size()));
    out_descriptor->algorithm_name = kCameraAlgorithmName;
    out_descriptor->cpu_available = true;
    out_descriptor->gpu_available = false;
    out_descriptor->data_contract.arrays_to_allocate = {
      {"camera_position", 1u, static_cast<uint32_t>(sizeof(Vec3))},
      {"camera_target", 1u, static_cast<uint32_t>(sizeof(Vec3))},
      {"camera_up", 1u, static_cast<uint32_t>(sizeof(Vec3))},
    };
    out_descriptor->data_contract.temporary_registers_to_allocate = {
      {"camera_view_registers", 1u, static_cast<uint32_t>(sizeof(float) * 16u)},
      {"camera_projection_registers", 1u, static_cast<uint32_t>(sizeof(float) * 16u)},
    };
    out_descriptor->data_contract.temporary_caches_to_allocate = {
      {"camera_sample_cache", point_count, static_cast<uint32_t>(sizeof(Vec3))},
    };
    out_descriptor->data_contract.filled_data_formats = {
      {"camera_view_state", 1u, static_cast<uint32_t>(sizeof(float) * 16u)},
    };
    out_descriptor->data_contract.algorithm_required_formats = {
      {"camera_input_mesh", point_count, static_cast<uint32_t>(sizeof(Vec3))},
    };
    return true;
  }

  bool ReflectReadableParameters(
    const AlgorithmContainerDescriptor& container_descriptor,
    AlgorithmReadableReflection* out_reflection) const override {
    if (!out_reflection) return false;

    out_reflection->algorithm_name = container_descriptor.algorithm_name.empty()
      ? kCameraAlgorithmName
      : container_descriptor.algorithm_name;
    out_reflection->fields = {
      {"algorithm_name", out_reflection->algorithm_name},
      {"cpu_available", container_descriptor.cpu_available ? "true" : "false"},
      {"gpu_available", container_descriptor.gpu_available ? "true" : "false"},
      {"motion_radius", std::to_string(container_descriptor.motion_radius)},
      {"arrays_to_allocate", std::to_string(container_descriptor.data_contract.arrays_to_allocate.size())},
      {"required_formats", std::to_string(container_descriptor.data_contract.algorithm_required_formats.size())},
    };
    out_reflection->valid = true;
    return true;
  }

  bool ReflectDecomposition(
    const AlgorithmContainerDescriptor& container_descriptor,
    AlgorithmDecompositionReflection* out_reflection) const override {
    if (!out_reflection) return false;

    out_reflection->algorithm_name = container_descriptor.algorithm_name.empty()
      ? kCameraAlgorithmName
      : container_descriptor.algorithm_name;
    out_reflection->required_resources = {"mesh", "camera"};
    if (out_reflection->required_resources.empty()) {
      out_reflection->required_resources = {"mesh", "camera"};
    }
    out_reflection->valid = true;
    return true;
  }
};

}  // namespace algorithm_library

using algorithm_library::CameraAlgorithmPackageCodec;
using algorithm_library::kCameraAlgorithmName;
