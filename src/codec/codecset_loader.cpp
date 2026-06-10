#include "codec/codec_protocol.h"
#include "agent_management/agent_abi.h"

#include "algorithm_management/algorithm_manager.h"

#include <memory>
#include <utility>

namespace codec {

bool CreateAlgorithmCodecGroupByName(
  const std::string& algorithm_name,
  agent::AgentAlgorithmCodecGroup* out_group,
  std::string* out_error_message) {
  if (!out_group) {
    if (out_error_message) {
      *out_error_message = "AgentAlgorithmCodecGroup output pointer is null.";
    }
    return false;
  }

  agent::AgentAlgorithmCodecGroup group{};
  group.algorithm_profile.algorithm_name = algorithm_name;
  group.algorithm_profile.container_manifest_name = algorithm_name;

  AlgorithmPluginComponents plugin_components{};
  std::string plugin_error_message;
  if (TryLoadAlgorithmPluginComponents(algorithm_name, &plugin_components, &plugin_error_message)) {
    group.cpu_symbol = plugin_components.cpu_symbol;
    group.gpu_symbol = plugin_components.gpu_symbol;
    group.reflector = plugin_components.reflector;
    group.decomposer = plugin_components.decomposer;
    group.intervention = plugin_components.intervention;
    group.temporaryTest_main_thread_executor = plugin_components.temporary_test_executor;
    if (plugin_components.runtime_reflector) {
      group.algorithm_reflector = std::move(plugin_components.runtime_reflector);
    }
    if (!group.algorithm_reflector) {
      algorithm::AlgorithmReflector runtime_reflector{};
      if (algorithm_management::TryCreateAlgorithmReflectorFromAlgorithmName(
            algorithm_name,
            &runtime_reflector,
            nullptr)) {
        group.algorithm_reflector = std::make_shared<algorithm::AlgorithmReflector>(std::move(runtime_reflector));
      }
    }
    *out_group = std::move(group);
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }
  if (!plugin_error_message.empty()) {
    if (out_error_message) {
      *out_error_message = std::move(plugin_error_message);
    }
    return false;
  }

  group.cpu_symbol = true;
  group.gpu_symbol = true;

  if (!CreateAlgorithmPackageReflectorByName(
        algorithm_name,
        &group.reflector,
        out_error_message)) {
    return false;
  }
  if (!CreateAlgorithmPackageDecomposerByName(
        algorithm_name,
        &group.decomposer,
        out_error_message)) {
    return false;
  }
  if (!CreateAlgorithmInterventionByName(
        algorithm_name,
        &group.intervention,
        out_error_message)) {
    return false;
  }
  algorithm::AlgorithmReflector runtime_reflector{};
  if (algorithm_management::TryCreateAlgorithmReflectorFromAlgorithmName(
        algorithm_name,
        &runtime_reflector,
        nullptr)) {
    group.algorithm_reflector = std::make_shared<algorithm::AlgorithmReflector>(std::move(runtime_reflector));
  }

  *out_group = std::move(group);
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

}  // namespace codec
