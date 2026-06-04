#pragma once

#include "algorithm/algorithm_container_manifest.h"
#include "algorithm/algorithm_types.h"

namespace algorithm {

inline std::string AlgorithmMng_ResolveContainerName(
  const AlgorithmContainerDescriptor& container_descriptor,
  const std::string& package_name,
  const std::string& source_name) {
  return ResolveAlgorithmContainerName(container_descriptor, package_name, source_name);
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

}  // namespace algorithm

using algorithm::AlgorithmMng_LoadContainerDescriptorFromJsonFile;
using algorithm::AlgorithmMng_LoadContainerDescriptorFromJsonText;
using algorithm::AlgorithmMng_ResolveContainerName;
