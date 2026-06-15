#pragma once

#include "algorithm_management/algorithm_abi.h"
#include "algorithm_support/algorithm_data.h"

#include <memory>
#include <string>

namespace algorithm {
class AlgorithmReflector;
struct AlgorithmPackageLocation;
}  // namespace algorithm

namespace algorithm_support {

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

  agent::IAlgorithmIntervention* intervention{nullptr};
  void (*destroy_intervention)(agent::IAlgorithmIntervention*){nullptr};

  agent::IAlgorithmtemporaryTestMainThreadExecutor* temporary_test_executor{nullptr};
  void (*destroy_temporary_test_executor)(agent::IAlgorithmtemporaryTestMainThreadExecutor*){nullptr};

  void Clear() {
    api_version = kAlgorithmPluginApiVersion;
    cpu_symbol = true;
    gpu_symbol = true;
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
  std::shared_ptr<agent::IAlgorithmIntervention> intervention{};
  std::shared_ptr<agent::IAlgorithmtemporaryTestMainThreadExecutor> temporary_test_executor{};
};

bool TryLoadAlgorithmPluginComponents(
  const algorithm::AlgorithmPackageLocation& package_location,
  AlgorithmPluginComponents* out_components,
  std::string* out_error_message = nullptr);

bool CreateAlgorithmPackageRuntimeReflectorByName(
  const std::string& algorithm_name,
  std::shared_ptr<algorithm::AlgorithmReflector>* out_reflector,
  std::string* out_error_message = nullptr);

bool CreateAlgorithmPackageContainerSetFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  std::shared_ptr<algorithm::AlgorithmContainerSet>* out_container_set,
  std::string* out_error_message = nullptr);

bool QueryAlgorithmPackageRequestedBindingsFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  algorithm_management::AlgorithmRequestedResources* out_requested_resources,
  algorithm_management::AlgorithmRequestedDescriptorBindings* out_requested_descriptor_bindings,
  std::string* out_error_message = nullptr);

bool LoadAlgorithmPackageDefaultBindingsFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  std::vector<algorithm_management::AlgorithmResourceBinding>* out_resource_bindings,
  std::vector<algorithm_management::AlgorithmDescriptorValue>* out_descriptor_values,
  bool* out_has_default_file = nullptr,
  std::string* out_error_message = nullptr);

bool DecomposeAlgorithmPackageFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  const std::vector<algorithm_management::AlgorithmResourceBinding>& resource_bindings,
  const std::vector<algorithm_management::AlgorithmDescriptorValue>& descriptor_values,
  algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message = nullptr);

bool CreateAlgorithmInterventionByName(
  const std::string& algorithm_name,
  std::shared_ptr<agent::IAlgorithmIntervention>* out_intervention,
  std::string* out_error_message = nullptr);

bool CreateAlgorithmObjectFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  agent::AlgorithmObject* out_group,
  std::string* out_error_message = nullptr);

}  // namespace algorithm_support

namespace algorithm_library_plugin = algorithm_support;

namespace algorithm_management {
using algorithm_support::AlgorithmPluginBundle;
using algorithm_support::AlgorithmPluginComponents;
using algorithm_support::AlgorithmPluginCreateBundleFn;
using algorithm_support::AlgorithmPluginCreateRuntimeReflectorFn;
using algorithm_support::AlgorithmPluginRequest;
}  // namespace algorithm_management

#if defined(ALGORITHM_LIBRARY_PLUGIN_BUILD)
#define ALGORITHM_LIBRARY_PLUGIN_API __declspec(dllexport)
#else
#define ALGORITHM_LIBRARY_PLUGIN_API __declspec(dllimport)
#endif

extern "C" {

ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateBundle(
  const algorithm_support::AlgorithmPluginRequest* request,
  algorithm_support::AlgorithmPluginBundle* out_bundle);

ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateRuntimeReflector(
  const algorithm_support::AlgorithmPluginRequest* request,
  algorithm::AlgorithmReflector* out_reflector);

}

