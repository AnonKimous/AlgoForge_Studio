#pragma once

#include "algorithm_support/algorithm_types.h"

#include <string>
#include <unordered_map>
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
  std::unordered_map<std::string, std::vector<std::string>> container_aliases_by_name;
  std::vector<AlgorithmReflectorManifestItem> reflectors;
};

}  // namespace algorithm
