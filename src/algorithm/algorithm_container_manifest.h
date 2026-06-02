#pragma once

#include "algorithm_types.h"

#include <string>
#include <vector>

namespace algorithm {

struct AlgorithmContainerManifestItem {
  std::string name;
  std::string kind{"scalar"};
  std::string precision;
  std::vector<uint32_t> shape;
  uint32_t count{1};
  bool count_specified{false};
  std::string count_from;
};

struct AlgorithmContainerManifest {
  std::string algorithm_name;
  std::string solve_precision{"fp32"};
  std::vector<AlgorithmContainerManifestItem> variables;
  std::vector<AlgorithmContainerManifestItem> variable_arrays;
};

bool LoadAlgorithmContainerManifestFromJsonText(
  const std::string& json_text,
  AlgorithmContainerManifest* out_manifest,
  std::string* out_error_message = nullptr);

bool LoadAlgorithmContainerManifestFromJsonFile(
  const std::string& path,
  AlgorithmContainerManifest* out_manifest,
  std::string* out_error_message = nullptr);

AlgorithmContainerDescriptor BuildAlgorithmContainerDescriptor(const AlgorithmContainerManifest& manifest);

bool LoadAlgorithmContainerDescriptorFromJsonText(
  const std::string& json_text,
  AlgorithmContainerDescriptor* out_descriptor,
  std::string* out_error_message = nullptr);

bool LoadAlgorithmContainerDescriptorFromJsonFile(
  const std::string& path,
  AlgorithmContainerDescriptor* out_descriptor,
  std::string* out_error_message = nullptr);

}  // namespace algorithm

using algorithm::AlgorithmContainerManifest;
using algorithm::AlgorithmContainerManifestItem;
using algorithm::BuildAlgorithmContainerDescriptor;
using algorithm::LoadAlgorithmContainerDescriptorFromJsonFile;
using algorithm::LoadAlgorithmContainerDescriptorFromJsonText;
using algorithm::LoadAlgorithmContainerManifestFromJsonFile;
using algorithm::LoadAlgorithmContainerManifestFromJsonText;
