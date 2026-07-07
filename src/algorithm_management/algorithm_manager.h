#pragma once

#include <chrono>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "algorithm_catalog/algorithm_catalog.h"
#include "algorithm_catalog/algorithm_library_paths.h"
#include "common_data/kernel_cfg.h"

#include "algorithm_management/algorithm_scheduler_runtime.h"

namespace algorithmManager {
inline void SetAlgorithmLibraryRuntimeBuildFlavor(
  algorithm::library_paths::AlgorithmLibraryRuntimeBuildFlavor flavor) {
  algorithm::library_paths::SetAlgorithmLibraryRuntimeBuildFlavor(flavor);
}

inline algorithm::library_paths::AlgorithmLibraryRuntimeBuildFlavor GetAlgorithmLibraryRuntimeBuildFlavor() {
  return algorithm::library_paths::GetAlgorithmLibraryRuntimeBuildFlavor();
}

inline std::filesystem::path ResolveAlgorithmLibrarySourceRoot() {
  return algorithm::library_paths::ResolveAlgorithmLibrarySourceRoot();
}

inline std::filesystem::path ResolveProjectRootFromAlgorithmLibraryRoot(
  const std::filesystem::path& algorithm_library_root) {
  return algorithm::library_paths::ResolveProjectRootFromAlgorithmLibraryRoot(algorithm_library_root);
}

inline std::filesystem::path ResolveAlgorithmLibraryRuntimeRoot() {
  return algorithm::library_paths::ResolveAlgorithmLibraryRuntimeRoot();
}

inline std::filesystem::path ResolveAlgorithmLibraryRuntimeNormDebugInfoRoot() {
  return algorithm::library_paths::ResolveAlgorithmLibraryRuntimeNormDebugInfoRoot();
}

inline std::filesystem::path ResolveAlgorithmLibraryRuntimePipelineDebugInfoRoot() {
  return algorithm::library_paths::ResolveAlgorithmLibraryRuntimePipelineDebugInfoRoot();
}

inline std::filesystem::path ResolvePipelineRunnerArtifactRoot() {
  return algorithm::library_paths::ResolvePipelineRunnerArtifactRoot();
}

inline bool CreateAlgorithmObjectFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  ::agentmanager::agent::AlgorithmObject* out_group,
  std::string* out_error_message = nullptr,
  bool load_reflector = true) {
  return catalog::CreateAlgorithmObjectFromLocation(
    package_location,
    out_group,
    out_error_message,
    load_reflector);
}

inline bool LoadAlgorithmPackageDefaultBindingsFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  std::vector<scheduler::AlgorithmResourceBinding>* out_resource_bindings,
  std::vector<scheduler::AlgorithmDescriptorValue>* out_descriptor_values,
  bool* out_has_default_file = nullptr,
  std::string* out_error_message = nullptr) {
  return catalog::LoadAlgorithmPackageDefaultBindingsFromLocation(
    package_location,
    out_resource_bindings,
    out_descriptor_values,
    out_has_default_file,
    out_error_message);
}

inline bool LoadAlgorithmPackageTransferMapFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  std::shared_ptr<algorithm::AlgorithmRuntimeTransferMap>* out_transfer_map,
  bool* out_has_transfer_map = nullptr,
  std::string* out_error_message = nullptr) {
  return catalog::LoadAlgorithmPackageTransferMapFromLocation(
    package_location,
    out_transfer_map,
    out_has_transfer_map,
    out_error_message);
}

inline bool LoadAlgorithmPipelineWrapperSpecFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  catalog::AlgorithmPipelineWrapperSpec* out_wrapper_spec,
  std::string* out_error_message = nullptr) {
  return catalog::LoadAlgorithmPipelineWrapperSpecFromLocation(
    package_location,
    out_wrapper_spec,
    out_error_message);
}

inline bool QueryAlgorithmPackageRequestedBindingsFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  scheduler::AlgorithmRequestedResources* out_requested_resources,
  scheduler::AlgorithmRequestedDescriptorBindings* out_requested_descriptor_bindings,
  std::string* out_error_message = nullptr) {
  return catalog::QueryAlgorithmPackageRequestedBindingsFromLocation(
    package_location,
    out_requested_resources,
    out_requested_descriptor_bindings,
    out_error_message);
}

inline void ClearAlgorithmExecutionCaches() {
  scheduler::ClearAlgorithmExecutionCaches();
}

inline void ClearAlgorithmScheduler() {
  scheduler::ClearAlgorithmScheduler();
}

inline bool EnqueueMountedPipelineStage0Submission(
  std::vector<scheduler::AlgorithmObject>* algorithm_objects,
  const std::string& pipeline_name,
  const std::string& agent_name,
  const std::vector<scheduler::AlgorithmResourceBinding>& resource_bindings,
  const std::vector<scheduler::AlgorithmDescriptorValue>& descriptor_values,
  std::vector<scheduler::AlgorithmAssemblyState>* algorithm_assembly_states,
  std::string* out_error_message = nullptr,
  bool load_reflector = true) {
  return scheduler::EnqueueMountedPipelineStage0Submission(
    algorithm_objects,
    pipeline_name,
    agent_name,
    resource_bindings,
    descriptor_values,
    algorithm_assembly_states,
    out_error_message,
    load_reflector);
}

inline bool ExecuteJobsAlgorithmObject(
  const ::agentmanager::agent::AlgorithmObject& object,
  const ::agentmanager::agent::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  ::algorithm::AlgorithmContainerSet* container_set,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  ::agentmanager::agent::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message = nullptr) {
  return scheduler::ExecuteJobsAlgorithmObject(
    object,
    context,
    agent_to_algorithm_signal,
    container_set,
    out_algorithm_to_agent_signal,
    out_debug_state,
    out_error_message);
}

inline bool ExecuteVkAlgorithmObject(
  const ::agentmanager::agent::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  const ::agentmanager::agent::AgentTickContext& context,
  std::string* out_error_message = nullptr) {
  return scheduler::ExecuteVkAlgorithmObject(
    object,
    container_set,
    context,
    out_error_message);
}

inline bool ExecuteCudaAlgorithmObject(
  const ::agentmanager::agent::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  const ::agentmanager::agent::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  ::agentmanager::agent::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message = nullptr) {
  return scheduler::ExecuteCudaAlgorithmObject(
    object,
    container_set,
    context,
    agent_to_algorithm_signal,
    out_algorithm_to_agent_signal,
    out_debug_state,
    out_error_message);
}

inline bool FinalizeAlgorithmObject(
  const ::agentmanager::agent::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message = nullptr) {
  return scheduler::FinalizeAlgorithmObject(object, container_set, out_error_message);
}

inline bool HasExecutableVkAlgorithmStage(const ::agentmanager::agent::AlgorithmObject& object) {
  return scheduler::HasExecutableVkAlgorithmStage(object);
}

inline bool HasExecutableCudaAlgorithmStage(const ::agentmanager::agent::AlgorithmObject& object) {
  return scheduler::HasExecutableCudaAlgorithmStage(object);
}

inline bool LoadAlgorithmPackageDefaultBindings(
  const std::string& algorithm_name,
  std::vector<scheduler::AlgorithmResourceBinding>* out_resource_bindings,
  std::vector<scheduler::AlgorithmDescriptorValue>* out_descriptor_values,
  bool* out_has_default_file = nullptr,
  std::string* out_error_message = nullptr) {
  return scheduler::LoadAlgorithmPackageDefaultBindings(
    algorithm_name,
    out_resource_bindings,
    out_descriptor_values,
    out_has_default_file,
    out_error_message);
}

inline bool MountPipelineAlgorithmObjects(
  std::vector<scheduler::AlgorithmObject>* algorithm_objects,
  std::vector<scheduler::AgentAlgorithmRuntimeState>* inout_runtime_states,
  std::vector<scheduler::AlgorithmAssemblyState>* algorithm_assembly_states,
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* standard_shared_container_sets,
  const std::string& agent_name,
  const std::string& pipeline_name,
  const std::vector<scheduler::AlgorithmPipelineStageSubmission>& stage_submissions,
  scheduler::AlgorithmExecutionPreference execution_preference,
  scheduler::AlgorithmPipelineTopology topology,
  scheduler::AlgorithmPipelineSyncMode sync_mode,
  size_t* out_index = nullptr,
  std::string* out_error_message = nullptr,
  bool load_reflector = true) {
  return scheduler::MountPipelineAlgorithmObjects(
    algorithm_objects,
    inout_runtime_states,
    algorithm_assembly_states,
    standard_shared_container_sets,
    agent_name,
    pipeline_name,
    stage_submissions,
    execution_preference,
    topology,
    sync_mode,
    out_index,
    out_error_message,
    load_reflector);
}

inline bool PrepareAlgorithmObjectByName(
  const std::string& algorithm_name,
  const std::vector<scheduler::AlgorithmResourceBinding>& resource_bindings,
  const std::vector<scheduler::AlgorithmDescriptorValue>& descriptor_values,
  ::agentmanager::agent::AlgorithmObject* out_group,
  std::string* out_error_message = nullptr,
  bool load_reflector = true) {
  return scheduler::PrepareAlgorithmObjectByName(
    algorithm_name,
    resource_bindings,
    descriptor_values,
    out_group,
    out_error_message,
    load_reflector);
}

inline bool QueryAlgorithmRequestedBindings(
  const std::string& algorithm_name,
  scheduler::AlgorithmRequestedResources* out_requested_resources,
  scheduler::AlgorithmRequestedDescriptorBindings* out_requested_descriptor_bindings,
  std::string* out_error_message = nullptr) {
  return scheduler::QueryAlgorithmRequestedBindings(
    algorithm_name,
    out_requested_resources,
    out_requested_descriptor_bindings,
    out_error_message);
}

inline bool ReplayMountedPipelineDebug(
  std::vector<scheduler::AlgorithmObject>* algorithm_objects,
  size_t index,
  const ::agentmanager::agent::AgentTickContext& context,
  std::vector<scheduler::AgentAlgorithmRuntimeState>* algorithm_runtime_states,
  std::string* out_error_message = nullptr) {
  return scheduler::ReplayMountedPipelineDebug(
    algorithm_objects,
    index,
    context,
    algorithm_runtime_states,
    out_error_message);
}

inline void SetAlgorithmRuntimeShutdownHook() {
  scheduler::SetAlgorithmRuntimeShutdownHook();
}

inline bool SubmitAlgorithmObject(
  const ::agentmanager::agent::AlgorithmObject& object,
  const ::agentmanager::agent::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  ::algorithm::AlgorithmContainerSet* container_set,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  ::agentmanager::agent::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message = nullptr) {
  return scheduler::SubmitAlgorithmObject(
    object,
    context,
    agent_to_algorithm_signal,
    container_set,
    out_algorithm_to_agent_signal,
    out_debug_state,
    out_error_message);
}

inline bool SynchronizeVkAlgorithmObject(
  const ::agentmanager::agent::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message = nullptr) {
  return scheduler::SynchronizeVkAlgorithmObject(object, container_set, out_error_message);
}

inline bool SynchronizeCudaAlgorithmObject(
  const ::agentmanager::agent::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message = nullptr) {
  return scheduler::SynchronizeCudaAlgorithmObject(object, container_set, out_error_message);
}

inline bool TickMountedPipeline(
  std::vector<scheduler::AlgorithmObject>* algorithm_objects,
  size_t begin_index,
  size_t end_index,
  const std::string& agent_name,
  const ::agentmanager::agent::AgentTickContext& context,
  const std::vector<bool>& allow_tick_mask,
  const std::vector<scheduler::AlgorithmAssemblyState>& assembly_states,
  bool collect_timing_log,
  std::vector<scheduler::AgentAlgorithmRuntimeState>* out_updated_runtime_states,
  common_data::AlgorithmToAgentSignal* out_pipeline_signal,
  bool* out_mounted_pipeline_processing_failed,
  std::string* out_error_message = nullptr) {
  return scheduler::TickMountedPipeline(
    algorithm_objects,
    begin_index,
    end_index,
    agent_name,
    context,
    allow_tick_mask,
    assembly_states,
    collect_timing_log,
    out_updated_runtime_states,
    out_pipeline_signal,
    out_mounted_pipeline_processing_failed,
    out_error_message);
}

inline bool TryGetMountedPipelineRuntime(
  const std::string& pipeline_name,
  const std::string& agent_name,
  scheduler::JobsPipelineRuntimeState* out_runtime_state) {
  return scheduler::TryGetMountedPipelineRuntime(pipeline_name, agent_name, out_runtime_state);
}

inline bool TryGetMountedPipelineRegistration(
  const std::string& pipeline_name,
  scheduler::JobsPipelineRegistration* out_registration) {
  return scheduler::AlgorithmScheduler::Instance().TryGetPipelineRegistration(
    pipeline_name,
    out_registration);
}

inline void UnregisterMountedPipeline(
  const std::string& pipeline_name,
  const std::string& agent_name) {
  scheduler::UnregisterMountedPipeline(pipeline_name, agent_name);
}

// Public facade only.
// Upper layers must only consume symbols exported from this root node.
// Public facade declarations must be explicit; do not use `using` or
// `typedef` in this header to re-export symbols.
// Do not use `using namespace algorithmManager` or reach into
// `algorithmManager::scheduler` / `algorithmManager::catalog` from outside
// the algorithm management implementation subtree.
}  // namespace algorithmManager

