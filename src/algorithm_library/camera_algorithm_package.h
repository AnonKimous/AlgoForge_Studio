#pragma once

#include "algorithm_package.h"
#include "common_data/vector_types.h"

#include <algorithm>
#include <string>

namespace algorithm_library {

inline constexpr const char* kCameraAlgorithmName = "camera";

class CameraAlgorithmPackageCodec : public algorithm::ISimpleAlgorithmPackageCodec {
 public:
  bool BuildComplianceDescriptor(
    const codec::VolumeDescriptor& volume,
    AlgorithmComplianceDescriptor* out_descriptor) const override {
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
};

}  // namespace algorithm_library

using algorithm_library::CameraAlgorithmPackageCodec;
using algorithm_library::kCameraAlgorithmName;
