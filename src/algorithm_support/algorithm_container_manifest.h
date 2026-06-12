#pragma once

#include "algorithm_support/algorithm_types.h"

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

struct AlgorithmReflectorManifestItem {
  std::vector<std::string> container_names;
  std::string reflection_object_name;
  std::string filter_name;
};

struct AlgorithmContainerManifest {
  std::string algorithm_name;
  std::string solve_precision{"fp32"};
  AlgorithmStandardContainerLayout standard_layout{};
  std::vector<AlgorithmContainerManifestItem> variables;
  std::vector<AlgorithmContainerManifestItem> variable_arrays;
  std::vector<AlgorithmReflectorManifestItem> reflectors;
};

bool LoadAlgorithmContainerManifestFromJsonText(
  const std::string& json_text,
  AlgorithmContainerManifest* out_manifest,
  std::string* out_error_message = nullptr);

bool LoadAlgorithmContainerManifestFromJsonFile(
  const std::string& path,
  AlgorithmContainerManifest* out_manifest,
  std::string* out_error_message = nullptr);

bool CreateAlgorithmContainersFromManifest(
  const AlgorithmContainerManifest& manifest,
  AlgorithmContainerSet* out_container_set,
  std::string* out_error_message = nullptr);

bool CreateAlgorithmReflectorFromManifest(
  const AlgorithmContainerManifest& manifest,
  AlgorithmReflector* out_reflector,
  std::string* out_error_message = nullptr);

bool CreateAlgorithmContainersFromManifestFile(
  const std::string& path,
  AlgorithmContainerSet* out_container_set,
  std::string* out_error_message = nullptr);

bool CreateAlgorithmContainersFromManifestName(
  const std::string& manifest_name,
  AlgorithmContainerSet* out_container_set,
  std::string* out_error_message = nullptr);

bool CreateAlgorithmReflectorFromManifestFile(
  const std::string& path,
  AlgorithmReflector* out_reflector,
  std::string* out_error_message = nullptr);

bool CreateAlgorithmReflectorFromManifestName(
  const std::string& manifest_name,
  AlgorithmReflector* out_reflector,
  std::string* out_error_message = nullptr);

bool TryCreateAlgorithmReflectorFromAlgorithmName(
  const std::string& algorithm_name,
  AlgorithmReflector* out_reflector,
  std::string* out_error_message = nullptr);

}  // namespace algorithm
