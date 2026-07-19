#pragma once

#include "algomanager/bridge/algorithm_abi.h"
#include "algomanager/bridge/algorithm_data.h"
#include "algomanager/bridge/algorithm_interaction_protocol.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace algorithm {
class AlgorithmReflector;
struct AlgorithmPackageLocation;
}  // namespace algorithm

namespace algomanager { namespace algocatalog {

inline constexpr uint32_t kAlgorithmPluginApiVersion = 4u;

struct AlgorithmPluginRequest {
  uint32_t api_version{kAlgorithmPluginApiVersion};
  const char* algorithm_name{};
  const char* algorithm_library_root{};
  const char* algorithm_folder{};
};

struct AlgorithmPluginBundle {
  uint32_t api_version{kAlgorithmPluginApiVersion};
  // Execution-time resource requirements, not execution-path selectors.
  bool jobs_symbol{true};
  bool vk_symbol{true};
  bool cuda_symbol{true};
  // Optional package-side systems that the mainline may load.
  bool reflector{true};
  bool intervention{true};

  // Optional plugin-provided VK exec provider. When null, mainline may fall
  // back to the package `exec` schema when `vk_symbol` is enabled.
  algomanager::bridge::IAlgorithmVkExecutor* vk_executor{nullptr};
  void (*destroy_vk_executor)(algomanager::bridge::IAlgorithmVkExecutor*){nullptr};

  algomanager::bridge::IAlgorithmCudaExecutor* cuda_executor{nullptr};
  void (*destroy_cuda_executor)(algomanager::bridge::IAlgorithmCudaExecutor*){nullptr};

  algomanager::bridge::IAlgorithmJobsExecutor* jobs_executor{nullptr};
  void (*destroy_jobs_executor)(algomanager::bridge::IAlgorithmJobsExecutor*){nullptr};

  // ABI-tail extension: old version-4 plugins keep the field layout above.
  bool compatibility_symbol{false};
  algomanager::bridge::IAlgorithmCompatibilityExecutor* compatibility_executor{nullptr};
  void (*destroy_compatibility_executor)(algomanager::bridge::IAlgorithmCompatibilityExecutor*){nullptr};

  void Clear() {
    api_version = kAlgorithmPluginApiVersion;
    jobs_symbol = true;
    vk_symbol = true;
    cuda_symbol = true;
    reflector = true;
    intervention = true;
    vk_executor = nullptr;
    destroy_vk_executor = nullptr;
    cuda_executor = nullptr;
    destroy_cuda_executor = nullptr;
    jobs_executor = nullptr;
    destroy_jobs_executor = nullptr;
    compatibility_symbol = false;
    compatibility_executor = nullptr;
    destroy_compatibility_executor = nullptr;
  }
};

using AlgorithmPluginCreateBundleFn = bool (*)(
  const AlgorithmPluginRequest* request,
  AlgorithmPluginBundle* out_bundle);

using AlgorithmPluginCreateRuntimeReflectorFn = bool (*)(
  const AlgorithmPluginRequest* request,
  algorithm::AlgorithmReflector* out_reflector);

struct AlgorithmPluginComponents {
  // Execution-time resource requirements, not execution-path selectors.
  bool jobs_symbol{true};
  bool vk_symbol{true};
  bool cuda_symbol{true};
  bool compatibility_symbol{false};
  // Optional package-side systems that the mainline may load.
  bool reflector{true};
  bool intervention{true};

  std::shared_ptr<algorithm::AlgorithmReflector> runtime_reflector{};
  std::shared_ptr<algomanager::bridge::IAlgorithmVkExecutor> vk_executor{};
  std::shared_ptr<algomanager::bridge::IAlgorithmCudaExecutor> cuda_executor{};
  std::shared_ptr<algomanager::bridge::IAlgorithmCompatibilityExecutor> compatibility_executor{};
  std::shared_ptr<algomanager::bridge::IAlgorithmJobsExecutor> jobs_executor{};
};

bool TryLoadAlgorithmPluginComponents(
  const algorithm::AlgorithmPackageLocation& package_location,
  AlgorithmPluginComponents* out_components,
  std::string* out_error_message = nullptr);

bool LoadAlgorithmPackageReflectorFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  std::shared_ptr<algorithm::AlgorithmReflector>* out_reflector,
  std::string* out_error_message = nullptr);

bool LoadAlgorithmVkExecutorFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  std::shared_ptr<algomanager::bridge::IAlgorithmVkExecutor>* out_vk_executor,
  std::string* out_error_message = nullptr);


}  // namespace catalog
}  // namespace algomanager

#if defined(ALGORITHM_LIBRARY_PLUGIN_BUILD)
#define ALGORITHM_LIBRARY_PLUGIN_API __declspec(dllexport)
#else
#define ALGORITHM_LIBRARY_PLUGIN_API __declspec(dllimport)
#endif

extern "C" {

ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateBundle(
  const algomanager::algocatalog::AlgorithmPluginRequest* request,
  algomanager::algocatalog::AlgorithmPluginBundle* out_bundle);

ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateRuntimeReflector(
  const algomanager::algocatalog::AlgorithmPluginRequest* request,
  algorithm::AlgorithmReflector* out_reflector);
}


