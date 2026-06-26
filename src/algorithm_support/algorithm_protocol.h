#pragma once

#include "algorithm_support/algorithm_abi.h"
#include "algorithm_support/algorithm_data.h"
#include "algorithm_support/algorithm_intervention_support.h"

#include <memory>
#include <string>
#include <unordered_map>

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
  // Execution-time resource requirements, not execution-path selectors.
  bool cpu_symbol{true};
  bool gpu_symbol{true};
  // Optional package-side systems that the mainline may load.
  bool reflector{true};
  bool intervention{true};

  // Optional plugin-provided GPU exec provider. When null, mainline may fall
  // back to the package `exec` schema when `gpu_symbol` is enabled.
  agent::IAlgorithmGpuExecutor* gpu_executor{nullptr};
  void (*destroy_gpu_executor)(agent::IAlgorithmGpuExecutor*){nullptr};

  agent::IAlgorithmCpuExecutor* cpu_executor{nullptr};
  void (*destroy_cpu_executor)(agent::IAlgorithmCpuExecutor*){nullptr};

  void Clear() {
    api_version = kAlgorithmPluginApiVersion;
    cpu_symbol = true;
    gpu_symbol = true;
    reflector = true;
    intervention = true;
    gpu_executor = nullptr;
    destroy_gpu_executor = nullptr;
    cpu_executor = nullptr;
    destroy_cpu_executor = nullptr;
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
  bool cpu_symbol{true};
  bool gpu_symbol{true};
  // Optional package-side systems that the mainline may load.
  bool reflector{true};
  bool intervention{true};

  std::shared_ptr<algorithm::AlgorithmReflector> runtime_reflector{};
  std::shared_ptr<agent::IAlgorithmGpuExecutor> gpu_executor{};
  std::shared_ptr<agent::IAlgorithmCpuExecutor> cpu_executor{};
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

bool LoadAlgorithmGpuExecutorFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  std::shared_ptr<agent::IAlgorithmGpuExecutor>* out_gpu_executor,
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
  const algorithm_management::CpuPipelineInterStageBufferRuntimeState& inter_stage_buffer,
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
  algorithm_management::CpuPipelineInterStageBufferRuntimeState* inter_stage_buffer,
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* stage_container_sets,
  std::string* out_error_message = nullptr);

bool PipelineStageBridgeCaptureIngressDebugSet(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& pipeline_name,
  const std::string& target_stage_name,
  const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
  const algorithm::AlgorithmContainerSet& target_container_set,
  algorithm_management::PipelineStageBridgeDebugSet* out_debug_set,
  std::string* out_error_message = nullptr);

bool PipelineStageBridgeCaptureEgressDebugSet(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& pipeline_name,
  const std::string& source_stage_name,
  const algorithm::AlgorithmContainerSet& source_container_set,
  const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
  algorithm_management::PipelineStageBridgeDebugSet* in_out_debug_set,
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

bool CreateAlgorithmObjectFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  agent::AlgorithmObject* out_group,
  std::string* out_error_message = nullptr);

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
    const algorithm_management::CpuPipelineInterStageBufferRuntimeState& inter_stage_buffer,
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
    algorithm_management::CpuPipelineInterStageBufferRuntimeState* inter_stage_buffer,
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
    algorithm_management::PipelineStageBridgeDebugSet* out_debug_set,
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
    algorithm_management::PipelineStageBridgeDebugSet* in_out_debug_set,
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

}  // namespace algorithm_support

namespace algorithmManager {
namespace support {
using ::algorithm_support::kAlgorithmPluginApiVersion;
using ::algorithm_support::AlgorithmPluginBundle;
using ::algorithm_support::AlgorithmPluginComponents;
using ::algorithm_support::AlgorithmPluginCreateBundleFn;
using ::algorithm_support::AlgorithmPluginCreateRuntimeReflectorFn;
using ::algorithm_support::AlgorithmPipelineWrapperSpec;
using ::algorithm_support::AlgorithmPipelineWrapperStageSpec;
using ::algorithm_support::AlgorithmPluginRequest;
using ::algorithm_support::PipelineStageBridge;
using ::algorithm::AlgorithmRuntimeTransferBinding;
using ::algorithm::AlgorithmRuntimeTransferEdge;
using ::algorithm::AlgorithmRuntimeTransferMap;
using ::algorithm_support::PipelineStageBridgeIngress;
using ::algorithm_support::PipelineStageBridgeEgress;
using ::algorithm_support::PipelineStageBridgeCaptureIngressDebugSet;
using ::algorithm_support::PipelineStageBridgeCaptureEgressDebugSet;
using ::algorithm_support::LoadAlgorithmPackageTransferMapFromLocation;
using ::algorithm_support::TryLoadAlgorithmPluginComponents;
using ::algorithm_support::LoadAlgorithmPackageReflectorFromLocation;
using ::algorithm_support::LoadAlgorithmGpuExecutorFromLocation;
using ::algorithm_support::QueryAlgorithmPackageRequestedBindingsFromLocation;
using ::algorithm_support::LoadAlgorithmPackageDefaultBindingsFromLocation;
using ::algorithm_support::DecomposeAlgorithmPackageFromLocation;
using ::algorithm_support::LoadAlgorithmInterventionFromLocation;
using ::algorithm_support::CreateAlgorithmObjectFromLocation;
}  // namespace support
}  // namespace algorithmManager

namespace algorithm_management {
using algorithm_support::kAlgorithmPluginApiVersion;
using algorithm_support::AlgorithmPluginBundle;
using algorithm_support::AlgorithmPluginComponents;
using algorithm_support::AlgorithmPluginCreateBundleFn;
using algorithm_support::AlgorithmPipelineWrapperSpec;
using algorithm_support::AlgorithmPipelineWrapperStageSpec;
using algorithm_support::AlgorithmPluginRequest;
using algorithm_support::PipelineStageBridge;
using ::algorithm::AlgorithmRuntimeTransferBinding;
using ::algorithm::AlgorithmRuntimeTransferEdge;
using ::algorithm::AlgorithmRuntimeTransferMap;
using algorithm_support::PipelineStageBridgeIngress;
using algorithm_support::PipelineStageBridgeEgress;
using algorithm_support::PipelineStageBridgeCaptureIngressDebugSet;
using algorithm_support::PipelineStageBridgeCaptureEgressDebugSet;
using algorithm_support::LoadAlgorithmPackageTransferMapFromLocation;
using algorithm_support::TryLoadAlgorithmPluginComponents;
using algorithm_support::LoadAlgorithmPackageReflectorFromLocation;
using algorithm_support::LoadAlgorithmGpuExecutorFromLocation;
using algorithm_support::QueryAlgorithmPackageRequestedBindingsFromLocation;
using algorithm_support::DecomposeAlgorithmPackageFromLocation;
using algorithm_support::LoadAlgorithmInterventionFromLocation;
}  // namespace algorithm_management

namespace algorithm_library_plugin = algorithmManager::support;

#if defined(ALGORITHM_LIBRARY_PLUGIN_BUILD)
#define ALGORITHM_LIBRARY_PLUGIN_API __declspec(dllexport)
#else
#define ALGORITHM_LIBRARY_PLUGIN_API __declspec(dllimport)
#endif

extern "C" {

ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateBundle(
  const algorithmManager::support::AlgorithmPluginRequest* request,
  algorithmManager::support::AlgorithmPluginBundle* out_bundle);

ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateRuntimeReflector(
  const algorithmManager::support::AlgorithmPluginRequest* request,
  algorithm::AlgorithmReflector* out_reflector);
}

