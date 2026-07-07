#pragma once

#include "algorithm_catalog/algorithm_abi.h"
#include "algorithm_catalog/algorithm_data.h"
#include "algorithm_catalog/algorithm_intervention_support.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace algorithm {
class AlgorithmReflector;
struct AlgorithmPackageLocation;
}  // namespace algorithm

namespace algorithmManager { namespace catalog {

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
  agentmanager::agent::IAlgorithmVkExecutor* vk_executor{nullptr};
  void (*destroy_vk_executor)(agentmanager::agent::IAlgorithmVkExecutor*){nullptr};

  agentmanager::agent::IAlgorithmCudaExecutor* cuda_executor{nullptr};
  void (*destroy_cuda_executor)(agentmanager::agent::IAlgorithmCudaExecutor*){nullptr};

  agentmanager::agent::IAlgorithmJobsExecutor* jobs_executor{nullptr};
  void (*destroy_jobs_executor)(agentmanager::agent::IAlgorithmJobsExecutor*){nullptr};

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
  // Optional package-side systems that the mainline may load.
  bool reflector{true};
  bool intervention{true};

  std::shared_ptr<algorithm::AlgorithmReflector> runtime_reflector{};
  std::shared_ptr<agentmanager::agent::IAlgorithmVkExecutor> vk_executor{};
  std::shared_ptr<agentmanager::agent::IAlgorithmCudaExecutor> cuda_executor{};
  std::shared_ptr<agentmanager::agent::IAlgorithmJobsExecutor> jobs_executor{};
};

struct AlgorithmPipelineWrapperStageSpec {
  bool declared{false};
  std::string algorithm_name;
};

struct AlgorithmPipelineWrapperSpec {
  bool declared{false};
  AlgorithmPipelineWrapperStageSpec stage_begin{};
  AlgorithmPipelineWrapperStageSpec stage_end{};
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
  std::shared_ptr<agentmanager::agent::IAlgorithmVkExecutor>* out_vk_executor,
  std::string* out_error_message = nullptr);

bool LoadAlgorithmPackageTransferMapFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  std::shared_ptr<algorithm::AlgorithmRuntimeTransferMap>* out_transfer_map,
  bool* out_has_transfer_map = nullptr,
  std::string* out_error_message = nullptr);

bool PipelineStageBridgeIngress(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& target_stage_name,
  const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
  algorithm::AlgorithmContainerSet* out_target_container_set,
  std::string* out_error_message = nullptr);

bool PipelineStageBridgeIngress(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& target_stage_name,
  const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
  const algorithmManager::scheduler::JobsPipelineInterStageBufferRuntimeState& inter_stage_buffer,
  algorithm::AlgorithmContainerSet* out_target_container_set,
  std::string* out_error_message = nullptr);

bool PipelineStageBridgeEgress(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& source_stage_name,
  const algorithm::AlgorithmContainerSet& source_container_set,
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* stage_container_sets,
  std::string* out_error_message = nullptr);

bool PipelineStageBridgeEgress(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& source_stage_name,
  const algorithm::AlgorithmContainerSet& source_container_set,
  algorithmManager::scheduler::JobsPipelineInterStageBufferRuntimeState* inter_stage_buffer,
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* stage_container_sets,
  std::string* out_error_message = nullptr);

bool PipelineStageBridgeCaptureIngressDebugSet(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& pipeline_name,
  const std::string& target_stage_name,
  const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
  const algorithm::AlgorithmContainerSet& target_container_set,
  algorithmManager::scheduler::PipelineStageBridgeDebugSet* out_debug_set,
  std::string* out_error_message = nullptr);

bool PipelineStageBridgeCaptureEgressDebugSet(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& pipeline_name,
  const std::string& source_stage_name,
  const algorithm::AlgorithmContainerSet& source_container_set,
  const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
  algorithmManager::scheduler::PipelineStageBridgeDebugSet* in_out_debug_set,
  std::string* out_error_message = nullptr);

bool QueryAlgorithmPackageRequestedBindingsFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  algorithmManager::scheduler::AlgorithmRequestedResources* out_requested_resources,
  algorithmManager::scheduler::AlgorithmRequestedDescriptorBindings* out_requested_descriptor_bindings,
  std::string* out_error_message = nullptr);

bool LoadAlgorithmPackageDefaultBindingsFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  std::vector<algorithmManager::scheduler::AlgorithmResourceBinding>* out_resource_bindings,
  std::vector<algorithmManager::scheduler::AlgorithmDescriptorValue>* out_descriptor_values,
  bool* out_has_default_file = nullptr,
  std::string* out_error_message = nullptr);

bool DecomposeAlgorithmPackageFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  const std::vector<algorithmManager::scheduler::AlgorithmResourceBinding>& resource_bindings,
  const std::vector<algorithmManager::scheduler::AlgorithmDescriptorValue>& descriptor_values,
  algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message = nullptr);

bool CreateAlgorithmObjectFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  agentmanager::agent::AlgorithmObject* out_group,
  std::string* out_error_message = nullptr,
  bool load_reflector = true);

bool LoadAlgorithmPipelineWrapperSpecFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  AlgorithmPipelineWrapperSpec* out_wrapper_spec,
  std::string* out_error_message = nullptr);

class PipelineStageBridge {
 public:
  PipelineStageBridge() = default;
  explicit PipelineStageBridge(std::shared_ptr<algorithm::AlgorithmRuntimeTransferMap> transfer_map)
    : transfer_map_(std::move(transfer_map)) {}

  void SetTransferMap(std::shared_ptr<algorithm::AlgorithmRuntimeTransferMap> transfer_map) {
    transfer_map_ = std::move(transfer_map);
  }

  const std::shared_ptr<algorithm::AlgorithmRuntimeTransferMap>& transfer_map() const {
    return transfer_map_;
  }

  bool IngestFromPreviousStage(
    const std::string& target_stage_name,
    const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
    algorithm::AlgorithmContainerSet* out_target_container_set,
    std::string* out_error_message = nullptr) const {
    if (!transfer_map_) {
      if (out_error_message) {
        *out_error_message = "Algorithm runtime transfer map is unavailable.";
      }
      return false;
    }
    return PipelineStageBridgeIngress(
      *transfer_map_,
      target_stage_name,
      stage_container_sets,
      out_target_container_set,
      out_error_message);
  }

  bool IngestFromPreviousStage(
    const std::string& target_stage_name,
    const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
    const algorithmManager::scheduler::JobsPipelineInterStageBufferRuntimeState& inter_stage_buffer,
    algorithm::AlgorithmContainerSet* out_target_container_set,
    std::string* out_error_message = nullptr) const {
    if (!transfer_map_) {
      if (out_error_message) {
        *out_error_message = "Algorithm runtime transfer map is unavailable.";
      }
      return false;
    }
    return PipelineStageBridgeIngress(
      *transfer_map_,
      target_stage_name,
      stage_container_sets,
      inter_stage_buffer,
      out_target_container_set,
      out_error_message);
  }

  bool EmitToNextStage(
    const std::string& source_stage_name,
    const algorithm::AlgorithmContainerSet& source_container_set,
    std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* stage_container_sets,
    std::string* out_error_message = nullptr) const {
    if (!transfer_map_) {
      if (out_error_message) {
        *out_error_message = "Algorithm runtime transfer map is unavailable.";
      }
      return false;
    }
    return PipelineStageBridgeEgress(
      *transfer_map_,
      source_stage_name,
      source_container_set,
      stage_container_sets,
      out_error_message);
  }

  bool EmitToNextStage(
    const std::string& source_stage_name,
    const algorithm::AlgorithmContainerSet& source_container_set,
    algorithmManager::scheduler::JobsPipelineInterStageBufferRuntimeState* inter_stage_buffer,
    std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* stage_container_sets,
    std::string* out_error_message = nullptr) const {
    if (!transfer_map_) {
      if (out_error_message) {
        *out_error_message = "Algorithm runtime transfer map is unavailable.";
      }
      return false;
    }
    return PipelineStageBridgeEgress(
      *transfer_map_,
      source_stage_name,
      source_container_set,
      inter_stage_buffer,
      stage_container_sets,
      out_error_message);
  }

  bool CaptureIngressDebugSet(
    const std::string& pipeline_name,
    const std::string& target_stage_name,
    const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
    const algorithm::AlgorithmContainerSet& target_container_set,
    algorithmManager::scheduler::PipelineStageBridgeDebugSet* out_debug_set,
    std::string* out_error_message = nullptr) const {
    if (!transfer_map_) {
      if (out_error_message) {
        *out_error_message = "Algorithm runtime transfer map is unavailable.";
      }
      return false;
    }
    return PipelineStageBridgeCaptureIngressDebugSet(
      *transfer_map_,
      pipeline_name,
      target_stage_name,
      stage_container_sets,
      target_container_set,
      out_debug_set,
      out_error_message);
  }

  bool CaptureEgressDebugSet(
    const std::string& pipeline_name,
    const std::string& source_stage_name,
    const algorithm::AlgorithmContainerSet& source_container_set,
    const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
    algorithmManager::scheduler::PipelineStageBridgeDebugSet* in_out_debug_set,
    std::string* out_error_message = nullptr) const {
    if (!transfer_map_) {
      if (out_error_message) {
        *out_error_message = "Algorithm runtime transfer map is unavailable.";
      }
      return false;
    }
    return PipelineStageBridgeCaptureEgressDebugSet(
      *transfer_map_,
      pipeline_name,
      source_stage_name,
      source_container_set,
      stage_container_sets,
      in_out_debug_set,
      out_error_message);
  }

 private:
  std::shared_ptr<algorithm::AlgorithmRuntimeTransferMap> transfer_map_{};
};

}  // namespace catalog
namespace support = catalog;
}  // namespace algorithmManager

namespace algorithm_library_plugin = algorithmManager::catalog;

#if defined(ALGORITHM_LIBRARY_PLUGIN_BUILD)
#define ALGORITHM_LIBRARY_PLUGIN_API __declspec(dllexport)
#else
#define ALGORITHM_LIBRARY_PLUGIN_API __declspec(dllimport)
#endif

extern "C" {

ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateBundle(
  const algorithmManager::catalog::AlgorithmPluginRequest* request,
  algorithmManager::catalog::AlgorithmPluginBundle* out_bundle);

ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateRuntimeReflector(
  const algorithmManager::catalog::AlgorithmPluginRequest* request,
  algorithm::AlgorithmReflector* out_reflector);
}


