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

#include "algomanager/bridge/algorithm_manifest_bridge.h"
#include "algomanager/bridge/algorithm_library_paths.h"
#include "common_data/kernel_cfg.h"

namespace algomanager {
using AlgorithmLibraryRuntimeBuildFlavor =
  algorithm::library_paths::AlgorithmLibraryRuntimeBuildFlavor;
using AlgorithmPipelineWrapperSpec = bridge::AlgorithmPipelineWrapperSpec;
using PipelineStageBridgeDebugBinding = bridge::PipelineStageBridgeDebugBinding;
using PipelineStageBridgeDebugSet = bridge::PipelineStageBridgeDebugSet;

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
  ::algomanager::bridge::AlgorithmObject* out_group,
  std::string* out_error_message = nullptr,
  bool load_reflector = true) {
  return bridge::CreateAlgorithmObjectFromLocation(
    package_location,
    out_group,
    out_error_message,
    load_reflector);
}

inline bool LoadAlgorithmPackageDefaultBindingsFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  std::vector<bridge::AlgorithmResourceBinding>* out_resource_bindings,
  std::vector<bridge::AlgorithmDescriptorValue>* out_descriptor_values,
  bool* out_has_default_file = nullptr,
  std::string* out_error_message = nullptr) {
  return bridge::LoadAlgorithmPackageDefaultBindingsFromLocation(
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
  return bridge::LoadAlgorithmPackageTransferMapFromLocation(
    package_location,
    out_transfer_map,
    out_has_transfer_map,
    out_error_message);
}

inline bool LoadAlgorithmPipelineWrapperSpecFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  bridge::AlgorithmPipelineWrapperSpec* out_wrapper_spec,
  std::string* out_error_message = nullptr) {
  return bridge::LoadAlgorithmPipelineWrapperSpecFromLocation(
    package_location,
    out_wrapper_spec,
    out_error_message);
}

inline bool QueryAlgorithmPackageRequestedBindingsFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  bridge::AlgorithmRequestedResources* out_requested_resources,
  bridge::AlgorithmRequestedDescriptorBindings* out_requested_descriptor_bindings,
  std::string* out_error_message = nullptr) {
  return bridge::QueryAlgorithmPackageRequestedBindingsFromLocation(
    package_location,
    out_requested_resources,
    out_requested_descriptor_bindings,
    out_error_message);
}

void ClearAlgorithmExecutionCaches();

void ClearAlgorithmScheduler();

void BeginDebugToolRecording();

void EndDebugToolRecording();

bool DebugToolRecording();

uint64_t DebugToolRecordingTickCount();

void RecordDebugToolTick();

bool EnqueueMountedPipelineStage0Submission(
  std::vector<bridge::AlgorithmObject>* algorithm_objects,
  const std::string& pipeline_name,
  const std::string& agent_name,
  const std::vector<bridge::AlgorithmResourceBinding>& resource_bindings,
  const std::vector<bridge::AlgorithmDescriptorValue>& descriptor_values,
  std::vector<bridge::AlgorithmAssemblyState>* algorithm_assembly_states,
  std::string* out_error_message = nullptr,
  bool load_reflector = true);

bool EnqueueMountedPipelineStage0SubmissionNode(
  ::algomanager::bridge::AlgorithmObject* pipeline_node,
  std::vector<bridge::AlgorithmAssemblyState>* inout_assembly_states,
  const std::string& agent_name,
  const std::vector<bridge::AlgorithmResourceBinding>& resource_bindings,
  const std::vector<bridge::AlgorithmDescriptorValue>& descriptor_values,
  std::string* out_error_message = nullptr,
  bool load_reflector = true);

void UnregisterMountedPipelineObjects(
  const std::vector<::algomanager::bridge::AlgorithmObject>& objects,
  const std::string& agent_name);

void UnregisterMountedPipelineObject(
  const ::algomanager::bridge::AlgorithmObject& object,
  const std::string& agent_name);

void RefreshAlgorithmObjectSignals(
  ::algomanager::bridge::AlgorithmObject& object,
  ::algomanager::bridge::AgentAlgorithmRuntimeState& runtime_state,
  const ::algomanager::bridge::AgentTickContext& context);

bool TickAlgorithmObject(
  ::algomanager::bridge::AlgorithmObject& object,
  ::algomanager::bridge::AgentAlgorithmRuntimeState& runtime_state,
  const std::string& agent_name,
  const ::algomanager::bridge::AgentTickContext& context,
  bool allow_tick,
  const bridge::AlgorithmAssemblyState& assembly_state,
  bool collect_timing_log,
  std::string* out_error_message = nullptr);

bool ExecuteJobsAlgorithmObject(
  const ::algomanager::bridge::AlgorithmObject& object,
  const ::algomanager::bridge::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  ::algorithm::AlgorithmContainerSet* container_set,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  ::algomanager::bridge::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message = nullptr);

bool ExecuteVkAlgorithmObject(
  const ::algomanager::bridge::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  const ::algomanager::bridge::AgentTickContext& context,
  std::string* out_error_message = nullptr);

bool ExecuteCudaAlgorithmObject(
  const ::algomanager::bridge::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  const ::algomanager::bridge::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  ::algomanager::bridge::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message = nullptr);

bool FinalizeAlgorithmObject(
  const ::algomanager::bridge::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message = nullptr);

bool HasExecutableVkAlgorithmStage(const ::algomanager::bridge::AlgorithmObject& object);

bool HasExecutableCudaAlgorithmStage(const ::algomanager::bridge::AlgorithmObject& object);

bool LoadAlgorithmPackageDefaultBindings(
  const std::string& algorithm_name,
  std::vector<bridge::AlgorithmResourceBinding>* out_resource_bindings,
  std::vector<bridge::AlgorithmDescriptorValue>* out_descriptor_values,
  bool* out_has_default_file = nullptr,
  std::string* out_error_message = nullptr);

bool MountPipelineAlgorithmObjects(
  std::vector<bridge::AlgorithmObject>* algorithm_objects,
  std::vector<bridge::AgentAlgorithmRuntimeState>* inout_runtime_states,
  std::vector<bridge::AlgorithmAssemblyState>* algorithm_assembly_states,
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* standard_shared_container_sets,
  const std::string& agent_name,
  const std::string& pipeline_name,
  const std::vector<bridge::AlgorithmPipelineStageSubmission>& stage_submissions,
  bridge::AlgorithmExecutionPreference execution_preference,
  bridge::AlgorithmPipelineTopology topology,
  bridge::AlgorithmPipelineSyncMode sync_mode,
  size_t* out_index = nullptr,
  std::string* out_error_message = nullptr,
  bool load_reflector = true);

bool PrepareAlgorithmObjectByName(
  const std::string& algorithm_name,
  const std::vector<bridge::AlgorithmResourceBinding>& resource_bindings,
  const std::vector<bridge::AlgorithmDescriptorValue>& descriptor_values,
  ::algomanager::bridge::AlgorithmObject* out_group,
  std::string* out_error_message = nullptr,
  bool load_reflector = true);

bool QueryAlgorithmRequestedBindings(
  const std::string& algorithm_name,
  bridge::AlgorithmRequestedResources* out_requested_resources,
  bridge::AlgorithmRequestedDescriptorBindings* out_requested_descriptor_bindings,
  std::string* out_error_message = nullptr);

bool ReplayMountedPipelineDebug(
  std::vector<bridge::AlgorithmObject>* algorithm_objects,
  size_t index,
  const ::algomanager::bridge::AgentTickContext& context,
  std::vector<bridge::AgentAlgorithmRuntimeState>* algorithm_runtime_states,
  std::string* out_error_message = nullptr);

void SetAlgorithmRuntimeShutdownHook();

bool SubmitAlgorithmObject(
  const ::algomanager::bridge::AlgorithmObject& object,
  const ::algomanager::bridge::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  ::algorithm::AlgorithmContainerSet* container_set,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  ::algomanager::bridge::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message = nullptr);

bool SynchronizeVkAlgorithmObject(
  const ::algomanager::bridge::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message = nullptr);

bool SynchronizeCudaAlgorithmObject(
  const ::algomanager::bridge::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message = nullptr);

bool TickMountedPipeline(
  std::vector<bridge::AlgorithmObject>* algorithm_objects,
  size_t begin_index,
  size_t end_index,
  const std::string& agent_name,
  const ::algomanager::bridge::AgentTickContext& context,
  const std::vector<bool>& allow_tick_mask,
  const std::vector<bridge::AlgorithmAssemblyState>& assembly_states,
  bool collect_timing_log,
  std::vector<bridge::AgentAlgorithmRuntimeState>* out_updated_runtime_states,
  common_data::AlgorithmToAgentSignal* out_pipeline_signal,
  bool* out_mounted_pipeline_processing_failed,
  std::string* out_error_message = nullptr);

bool ExecuteCompatibilityAlgorithmObject(
  const ::algomanager::bridge::AlgorithmObject& object,
  const ::algomanager::bridge::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  ::algorithm::AlgorithmContainerSet* container_set,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  ::algomanager::bridge::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message = nullptr);

bool ReplayMountedPipelineDebugNode(
  ::algomanager::bridge::AlgorithmObject* pipeline_node,
  ::algomanager::bridge::AgentAlgorithmRuntimeState* inout_runtime_state,
  size_t child_index,
  const ::algomanager::bridge::AgentTickContext& context,
  std::string* out_error_message = nullptr);

bool TickMountedPipelineNode(
  ::algomanager::bridge::AlgorithmObject* pipeline_node,
  ::algomanager::bridge::AgentAlgorithmRuntimeState* inout_runtime_state,
  const std::string& agent_name,
  const ::algomanager::bridge::AgentTickContext& context,
  bool allow_tick,
  const bridge::AlgorithmAssemblyState& assembly_state,
  bool collect_timing_log,
  common_data::AlgorithmToAgentSignal* out_pipeline_signal,
  bool* out_mounted_pipeline_processing_failed,
  std::string* out_error_message = nullptr);

bool TryGetMountedPipelineRuntime(
  const std::string& pipeline_name,
  const std::string& agent_name,
  bridge::JobsPipelineRuntimeState* out_runtime_state);

bool TryGetMountedPipelineRegistration(
  const std::string& pipeline_name,
  bridge::JobsPipelineRegistration* out_registration);

void UnregisterMountedPipeline(
  const std::string& pipeline_name,
  const std::string& agent_name);
// Public facade only.
// Upper layers must only consume symbols exported from this root node.
// Public facade declarations must be explicit; do not use `using` or
// `typedef` in this header to re-export symbols.
// Do not use `using namespace algomanager` or reach into
// `algomanager::algoscheduler` / `algomanager::algocatalog` from outside
// the algorithm management implementation subtree.
}  // namespace algomanager

