#pragma once

#include "algorithm/algorithm_container_manifest.h"
#include "algorithm_types.h"
#include "../algorithm/physics_algorithm_pipeline.h"

namespace algorithm_library {

inline bool AlgorithmMng_Run(const PhysicsAlgorithmRequest& request, PhysicsAlgorithmResult* result) {
  return PhysicsAlgorithmPipeline_Run(request, result);
}

inline std::string AlgorithmMng_ResolveContainerName(
  const AlgorithmContainerDescriptor& container_descriptor,
  const std::string& package_name,
  const std::string& source_name) {
  return ResolveAlgorithmContainerName(container_descriptor, package_name, source_name);
}

inline std::string AlgorithmMng_ResolveContainerName(
  const PhysicsAlgorithmRequest& request,
  const std::string& package_name,
  const std::string& source_name) {
  return ResolveAlgorithmContainerName(request.compliance_descriptor, package_name, source_name);
}

inline bool AlgorithmMng_LoadContainerDescriptorFromJsonText(
  const std::string& json_text,
  AlgorithmContainerDescriptor* out_descriptor,
  std::string* out_error_message = nullptr) {
  return LoadAlgorithmContainerDescriptorFromJsonText(json_text, out_descriptor, out_error_message);
}

inline bool AlgorithmMng_LoadContainerDescriptorFromJsonFile(
  const std::string& path,
  AlgorithmContainerDescriptor* out_descriptor,
  std::string* out_error_message = nullptr) {
  return LoadAlgorithmContainerDescriptorFromJsonFile(path, out_descriptor, out_error_message);
}

}  // namespace algorithm_library

using algorithm_library::AlgorithmMng_Run;
using algorithm_library::AlgorithmMng_LoadContainerDescriptorFromJsonFile;
using algorithm_library::AlgorithmMng_LoadContainerDescriptorFromJsonText;
using algorithm_library::AlgorithmMng_ResolveContainerName;
