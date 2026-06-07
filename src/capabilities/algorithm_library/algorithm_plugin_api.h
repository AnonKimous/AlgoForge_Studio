#pragma once

#include "algorithm_management/algorithm_manager.h"

#include <cstdint>

namespace algorithm {
struct AlgorithmReflector;
}  // namespace algorithm

namespace agent {
class IAlgorithmPackageCodec;
class IAlgorithmPackageDecomposer;
class IAlgorithmInterventionPackageCodec;
class IAlgorithmInterventionPackageAgent;
class IAlgorithmInterventionPackageAlgorithm;
class IAlgorithmtemporaryTestMainThreadExecutor;
}  // namespace agent

namespace algorithm_library_plugin {

inline constexpr uint32_t kAlgorithmPluginApiVersion = 1u;

#if defined(ALGORITHM_LIBRARY_PLUGIN_BUILD)
#define ALGORITHM_LIBRARY_PLUGIN_API __declspec(dllexport)
#else
#define ALGORITHM_LIBRARY_PLUGIN_API __declspec(dllimport)
#endif

struct AlgorithmPluginRequest {
  uint32_t api_version{kAlgorithmPluginApiVersion};
  const char* algorithm_name{};
  const char* algorithm_library_root{};
  const char* algorithm_folder{};
};

struct AlgorithmPluginBundle {
  uint32_t api_version{kAlgorithmPluginApiVersion};
  bool cpu_symbol{true};
  bool gpu_symbol{true};

  agent::IAlgorithmPackageCodec* reflector{nullptr};
  void (*destroy_reflector)(agent::IAlgorithmPackageCodec*){nullptr};

  agent::IAlgorithmPackageDecomposer* decomposer{nullptr};
  void (*destroy_decomposer)(agent::IAlgorithmPackageDecomposer*){nullptr};

  agent::IAlgorithmInterventionPackageCodec* intervention_codec{nullptr};
  void (*destroy_intervention_codec)(agent::IAlgorithmInterventionPackageCodec*){nullptr};

  agent::IAlgorithmInterventionPackageAgent* intervention_agent{nullptr};
  void (*destroy_intervention_agent)(agent::IAlgorithmInterventionPackageAgent*){nullptr};

  agent::IAlgorithmInterventionPackageAlgorithm* intervention_algorithm{nullptr};
  void (*destroy_intervention_algorithm)(agent::IAlgorithmInterventionPackageAlgorithm*){nullptr};

  agent::IAlgorithmtemporaryTestMainThreadExecutor* temporary_test_executor{nullptr};
  void (*destroy_temporary_test_executor)(agent::IAlgorithmtemporaryTestMainThreadExecutor*){nullptr};

  void Clear() {
    api_version = kAlgorithmPluginApiVersion;
    cpu_symbol = true;
    gpu_symbol = true;
    reflector = nullptr;
    destroy_reflector = nullptr;
    decomposer = nullptr;
    destroy_decomposer = nullptr;
    intervention_codec = nullptr;
    destroy_intervention_codec = nullptr;
    intervention_agent = nullptr;
    destroy_intervention_agent = nullptr;
    intervention_algorithm = nullptr;
    destroy_intervention_algorithm = nullptr;
    temporary_test_executor = nullptr;
    destroy_temporary_test_executor = nullptr;
  }
};

using AlgorithmPluginCreateBundleFn = bool (*)(
  const AlgorithmPluginRequest* request,
  AlgorithmPluginBundle* out_bundle);

using AlgorithmPluginCreateRuntimeReflectorFn = bool (*)(
  const AlgorithmPluginRequest* request,
  algorithm::AlgorithmReflector* out_reflector);

}  // namespace algorithm_library_plugin

extern "C" {

ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateBundle(
  const algorithm_library_plugin::AlgorithmPluginRequest* request,
  algorithm_library_plugin::AlgorithmPluginBundle* out_bundle);

ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateRuntimeReflector(
  const algorithm_library_plugin::AlgorithmPluginRequest* request,
  algorithm::AlgorithmReflector* out_reflector);

}
