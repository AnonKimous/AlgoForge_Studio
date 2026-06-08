#pragma once

#include "capabilities/algorithm_library/algorithm_plugin_api.h"

#include <memory>
#include <string>

namespace codec {

struct AlgorithmPluginComponents {
  bool cpu_symbol{true};
  bool gpu_symbol{true};

  algorithm::AlgorithmReflector runtime_reflector{};
  bool has_runtime_reflector{false};

  std::shared_ptr<agent::IAlgorithmPackageCodec> reflector{};
  std::shared_ptr<agent::IAlgorithmPackageDecomposer> decomposer{};
  std::shared_ptr<agent::IAlgorithmIntervention> intervention{};
  std::shared_ptr<agent::IAlgorithmtemporaryTestMainThreadExecutor> temporary_test_executor{};
};

bool TryLoadAlgorithmPluginComponents(
  const std::string& algorithm_name,
  AlgorithmPluginComponents* out_components,
  std::string* out_error_message = nullptr);

}  // namespace codec
