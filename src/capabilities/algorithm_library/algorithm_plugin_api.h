#pragma once

#include "algorithm_management/algorithm_manager.h"

#include <cstdint>

namespace algorithm {
struct AlgorithmReflector;
}  // namespace algorithm

namespace agent {
class IAlgorithmPackageCodec;
class IAlgorithmPackageDecomposer;
class IAlgorithmIntervention;
class IAlgorithmtemporaryTestMainThreadExecutor;
}  // namespace agent

namespace algorithm_library_plugin {

inline constexpr uint32_t kAlgorithmPluginApiVersion = 3u;

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

  agent::IAlgorithmIntervention* intervention{nullptr};
  void (*destroy_intervention)(agent::IAlgorithmIntervention*){nullptr};

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
    intervention = nullptr;
    destroy_intervention = nullptr;
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
