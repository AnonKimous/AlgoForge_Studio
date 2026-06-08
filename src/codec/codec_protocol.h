#pragma once

#include "codec/codec_data.h"

#include <memory>
#include <string>

namespace algorithm {
class AlgorithmReflector;
}  // namespace algorithm

namespace agent {
class IAlgorithmPackageCodec;
class IAlgorithmPackageDecomposer;
class IAlgorithmIntervention;
class IAlgorithmtemporaryTestMainThreadExecutor;
struct AgentAlgorithmCodecGroup;
}  // namespace agent

namespace codec {

inline constexpr uint32_t kAlgorithmPluginApiVersion = 3u;

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

struct AlgorithmPluginComponents {
  bool cpu_symbol{true};
  bool gpu_symbol{true};

  std::shared_ptr<algorithm::AlgorithmReflector> runtime_reflector{};

  std::shared_ptr<agent::IAlgorithmPackageCodec> reflector{};
  std::shared_ptr<agent::IAlgorithmPackageDecomposer> decomposer{};
  std::shared_ptr<agent::IAlgorithmIntervention> intervention{};
  std::shared_ptr<agent::IAlgorithmtemporaryTestMainThreadExecutor> temporary_test_executor{};
};

bool TryLoadAlgorithmPluginComponents(
  const std::string& algorithm_name,
  AlgorithmPluginComponents* out_components,
  std::string* out_error_message = nullptr);

bool CreateAlgorithmPackageReflectorByName(
  const std::string& algorithm_name,
  std::shared_ptr<agent::IAlgorithmPackageCodec>* out_reflector,
  std::string* out_error_message = nullptr);

bool CreateAlgorithmPackageDecomposerByName(
  const std::string& algorithm_name,
  std::shared_ptr<agent::IAlgorithmPackageDecomposer>* out_decomposer,
  std::string* out_error_message = nullptr);

bool CreateAlgorithmInterventionByName(
  const std::string& algorithm_name,
  std::shared_ptr<agent::IAlgorithmIntervention>* out_intervention,
  std::string* out_error_message = nullptr);

bool CreateAlgorithmCodecGroupByName(
  const std::string& algorithm_name,
  agent::AgentAlgorithmCodecGroup* out_group,
  std::string* out_error_message = nullptr);

}  // namespace codec

namespace algorithm_library_plugin = codec;

#if defined(ALGORITHM_LIBRARY_PLUGIN_BUILD)
#define ALGORITHM_LIBRARY_PLUGIN_API __declspec(dllexport)
#else
#define ALGORITHM_LIBRARY_PLUGIN_API __declspec(dllimport)
#endif

extern "C" {

ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateBundle(
  const codec::AlgorithmPluginRequest* request,
  codec::AlgorithmPluginBundle* out_bundle);

ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateRuntimeReflector(
  const codec::AlgorithmPluginRequest* request,
  algorithm::AlgorithmReflector* out_reflector);

}
