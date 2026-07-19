#pragma once

#include "algomanager/bridge/algorithm_abi.h"
#include "algomanager/catalog/algorithm_container_manifest.h"
#include "algomanager/bridge/algorithm_data.h"
#include "algomanager/bridge/algorithm_interaction_protocol.h"
#include "algomanager/catalog/algorithm_intervention_support.h"
#include "algomanager/bridge/algorithm_package_location.h"
#include "algomanager/bridge/algorithm_protocol.h"
#include "algomanager/bridge/algorithm_types.h"
#include "algomanager/bridge/algorithm_library_paths.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace algomanager {
using ::algorithm::AlgorithmContainerManifest;
using ::algorithm::AlgorithmContainerManifestItem;
using ::algorithm::AlgorithmReflectorManifestItem;
using ::algorithm::AlgorithmPackageLocation;

namespace algocatalog {

struct AlgorithmPipelineWrapperStageSpec {
  bool declared{false};
  std::string algorithm_name;
};

struct AlgorithmPipelineWrapperSpec {
  bool declared{false};
  AlgorithmPipelineWrapperStageSpec stage_begin{};
  AlgorithmPipelineWrapperStageSpec stage_end{};
};

bool QueryAlgorithmPackageRequestedBindingsFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  bridge::AlgorithmRequestedResources* out_requested_resources,
  bridge::AlgorithmRequestedDescriptorBindings* out_requested_descriptor_bindings,
  std::string* out_error_message = nullptr);

bool LoadAlgorithmPackageDefaultBindingsFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  std::vector<bridge::AlgorithmResourceBinding>* out_resource_bindings,
  std::vector<bridge::AlgorithmDescriptorValue>* out_descriptor_values,
  bool* out_has_default_file = nullptr,
  std::string* out_error_message = nullptr);

bool LoadAlgorithmPackageTransferMapFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  std::shared_ptr<algorithm::AlgorithmRuntimeTransferMap>* out_transfer_map,
  bool* out_has_transfer_map = nullptr,
  std::string* out_error_message = nullptr);

bool DecomposeAlgorithmPackageFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  const std::vector<bridge::AlgorithmResourceBinding>& resource_bindings,
  const std::vector<bridge::AlgorithmDescriptorValue>& descriptor_values,
  algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message = nullptr);

bool CreateAlgorithmObjectFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  bridge::AlgorithmObject* out_group,
  std::string* out_error_message = nullptr,
  bool load_reflector = true);

bool LoadAlgorithmPipelineWrapperSpecFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  AlgorithmPipelineWrapperSpec* out_wrapper_spec,
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
  const bridge::JobsPipelineInterStageBufferRuntimeState& inter_stage_buffer,
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
  bridge::JobsPipelineInterStageBufferRuntimeState* inter_stage_buffer,
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* stage_container_sets,
  std::string* out_error_message = nullptr);

bool PipelineStageBridgeCaptureIngressDebugSet(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& pipeline_name,
  const std::string& target_stage_name,
  const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
  const algorithm::AlgorithmContainerSet& target_container_set,
  bridge::PipelineStageBridgeDebugSet* out_debug_set,
  std::string* out_error_message = nullptr);

bool PipelineStageBridgeCaptureEgressDebugSet(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& pipeline_name,
  const std::string& source_stage_name,
  const algorithm::AlgorithmContainerSet& source_container_set,
  const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
  bridge::PipelineStageBridgeDebugSet* in_out_debug_set,
  std::string* out_error_message = nullptr);

}  // namespace algocatalog
}

