#include "algomanager/bridge/algorithm_manifest_bridge.h"
#include "algomanager/bridge/algorithm_pipeline_bridge.h"
#include "algomanager/bridge/algorithm_protocol.h"
#include "algomanager/catalog/algorithm_catalog.h"

namespace algomanager { namespace bridge {

bool CreateAlgorithmObjectFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  algomanager::bridge::AlgorithmObject* out_group,
  std::string* out_error_message,
  bool load_reflector) {
  return algomanager::algocatalog::CreateAlgorithmObjectFromLocation(
    package_location,
    out_group,
    out_error_message,
    load_reflector);
}

bool QueryAlgorithmPackageRequestedBindingsFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  algomanager::bridge::AlgorithmRequestedResources* out_requested_resources,
  algomanager::bridge::AlgorithmRequestedDescriptorBindings* out_requested_descriptor_bindings,
  std::string* out_error_message) {
  return algomanager::algocatalog::QueryAlgorithmPackageRequestedBindingsFromLocation(
    package_location,
    out_requested_resources,
    out_requested_descriptor_bindings,
    out_error_message);
}

bool LoadAlgorithmPackageDefaultBindingsFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  std::vector<algomanager::bridge::AlgorithmResourceBinding>* out_resource_bindings,
  std::vector<algomanager::bridge::AlgorithmDescriptorValue>* out_descriptor_values,
  bool* out_has_default_file,
  std::string* out_error_message) {
  return algomanager::algocatalog::LoadAlgorithmPackageDefaultBindingsFromLocation(
    package_location,
    out_resource_bindings,
    out_descriptor_values,
    out_has_default_file,
    out_error_message);
}

bool LoadAlgorithmPackageTransferMapFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  std::shared_ptr<algorithm::AlgorithmRuntimeTransferMap>* out_transfer_map,
  bool* out_has_transfer_map,
  std::string* out_error_message) {
  return algomanager::algocatalog::LoadAlgorithmPackageTransferMapFromLocation(
    package_location,
    out_transfer_map,
    out_has_transfer_map,
    out_error_message);
}

bool DecomposeAlgorithmPackageFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  const std::vector<algomanager::bridge::AlgorithmResourceBinding>& resource_bindings,
  const std::vector<algomanager::bridge::AlgorithmDescriptorValue>& descriptor_values,
  algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message) {
  return algomanager::algocatalog::DecomposeAlgorithmPackageFromLocation(
    package_location,
    resource_bindings,
    descriptor_values,
    container_set,
    out_error_message);
}

bool LoadAlgorithmPipelineWrapperSpecFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  AlgorithmPipelineWrapperSpec* out_wrapper_spec,
  std::string* out_error_message) {
  algomanager::algocatalog::AlgorithmPipelineWrapperSpec catalog_spec{};
  const bool ok = algomanager::algocatalog::LoadAlgorithmPipelineWrapperSpecFromLocation(
    package_location,
    &catalog_spec,
    out_error_message);
  if (!ok) {
    return false;
  }
  out_wrapper_spec->declared = catalog_spec.declared;
  out_wrapper_spec->stage_begin.declared = catalog_spec.stage_begin.declared;
  out_wrapper_spec->stage_begin.algorithm_name = catalog_spec.stage_begin.algorithm_name;
  out_wrapper_spec->stage_end.declared = catalog_spec.stage_end.declared;
  out_wrapper_spec->stage_end.algorithm_name = catalog_spec.stage_end.algorithm_name;
  return true;
}

bool PipelineStageBridgeIngress(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& target_stage_name,
  const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
  algorithm::AlgorithmContainerSet* out_target_container_set,
  std::string* out_error_message) {
  return algomanager::algocatalog::PipelineStageBridgeIngress(
    transfer_map,
    target_stage_name,
    stage_container_sets,
    out_target_container_set,
    out_error_message);
}

bool PipelineStageBridgeIngress(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& target_stage_name,
  const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
  const algomanager::bridge::JobsPipelineInterStageBufferRuntimeState& inter_stage_buffer,
  algorithm::AlgorithmContainerSet* out_target_container_set,
  std::string* out_error_message) {
  return algomanager::algocatalog::PipelineStageBridgeIngress(
    transfer_map,
    target_stage_name,
    stage_container_sets,
    inter_stage_buffer,
    out_target_container_set,
    out_error_message);
}

bool PipelineStageBridgeEgress(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& source_stage_name,
  const algorithm::AlgorithmContainerSet& source_container_set,
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* stage_container_sets,
  std::string* out_error_message) {
  return algomanager::algocatalog::PipelineStageBridgeEgress(
    transfer_map,
    source_stage_name,
    source_container_set,
    stage_container_sets,
    out_error_message);
}

bool PipelineStageBridgeEgress(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& source_stage_name,
  const algorithm::AlgorithmContainerSet& source_container_set,
  algomanager::bridge::JobsPipelineInterStageBufferRuntimeState* inter_stage_buffer,
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* stage_container_sets,
  std::string* out_error_message) {
  return algomanager::algocatalog::PipelineStageBridgeEgress(
    transfer_map,
    source_stage_name,
    source_container_set,
    inter_stage_buffer,
    stage_container_sets,
    out_error_message);
}

bool PipelineStageBridgeCaptureIngressDebugSet(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& pipeline_name,
  const std::string& target_stage_name,
  const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
  const algorithm::AlgorithmContainerSet& target_container_set,
  algomanager::bridge::PipelineStageBridgeDebugSet* out_debug_set,
  std::string* out_error_message) {
  return algomanager::algocatalog::PipelineStageBridgeCaptureIngressDebugSet(
    transfer_map,
    pipeline_name,
    target_stage_name,
    stage_container_sets,
    target_container_set,
    out_debug_set,
    out_error_message);
}

bool PipelineStageBridgeCaptureEgressDebugSet(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& pipeline_name,
  const std::string& source_stage_name,
  const algorithm::AlgorithmContainerSet& source_container_set,
  const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
  algomanager::bridge::PipelineStageBridgeDebugSet* in_out_debug_set,
  std::string* out_error_message) {
  return algomanager::algocatalog::PipelineStageBridgeCaptureEgressDebugSet(
    transfer_map,
    pipeline_name,
    source_stage_name,
    source_container_set,
    stage_container_sets,
    in_out_debug_set,
    out_error_message);
}

}  // namespace bridge
}  // namespace algomanager
