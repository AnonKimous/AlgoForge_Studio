#pragma once

#include "algomanager/bridge/algorithm_abi.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace algomanager { namespace algoscheduler {

void ClearSchedulerState();
void BeginSchedulerDebugToolRecording();
void EndSchedulerDebugToolRecording();
bool SchedulerDebugToolRecording();
uint64_t SchedulerDebugToolRecordingTickCount();
void RecordSchedulerDebugToolTick();

void ClearAlgorithmExecutionCaches();

bool ExecuteJobsAlgorithmObject(
  const bridge::AlgorithmObject& object,
  const bridge::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  algorithm::AlgorithmContainerSet* container_set,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  bridge::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message);

bool ExecuteVkAlgorithmObject(
  const bridge::AlgorithmObject& object,
  algorithm::AlgorithmContainerSet* container_set,
  const bridge::AgentTickContext& context,
  std::string* out_error_message);

bool ExecuteCudaAlgorithmObject(
  const bridge::AlgorithmObject& object,
  algorithm::AlgorithmContainerSet* container_set,
  const bridge::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  bridge::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message);

bool ExecuteCompatibilityAlgorithmObject(
  const bridge::AlgorithmObject& object,
  const bridge::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  algorithm::AlgorithmContainerSet* container_set,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  bridge::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message);

bool FinalizeAlgorithmObject(
  const bridge::AlgorithmObject& object,
  algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message);

bool HasExecutableVkAlgorithmStage(const bridge::AlgorithmObject& object);
bool HasExecutableCudaAlgorithmStage(const bridge::AlgorithmObject& object);

bool SynchronizeVkAlgorithmObject(
  const bridge::AlgorithmObject& object,
  algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message);

bool SynchronizeCudaAlgorithmObject(
  const bridge::AlgorithmObject& object,
  algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message);

bool TickAlgorithmObjectForManager(
  bridge::AlgorithmObject& object,
  bridge::AgentAlgorithmRuntimeState& runtime_state,
  const std::string& agent_name,
  const bridge::AgentTickContext& context,
  bool allow_tick,
  const bridge::AlgorithmAssemblyState& assembly_state,
  bool collect_timing_log,
  std::string* out_error_message);

void RefreshAlgorithmObjectSignalsForManager(
  bridge::AlgorithmObject& object,
  bridge::AgentAlgorithmRuntimeState& runtime_state,
  const bridge::AgentTickContext& context);

bool LoadAlgorithmPackageDefaultBindingsForManager(
  const std::string& algorithm_name,
  std::vector<bridge::AlgorithmResourceBinding>* out_resource_bindings,
  std::vector<bridge::AlgorithmDescriptorValue>* out_descriptor_values,
  bool* out_has_default_file,
  std::string* out_error_message);

bool PrepareAlgorithmObjectByName(
  const std::string& algorithm_name,
  const std::vector<bridge::AlgorithmResourceBinding>& resource_bindings,
  const std::vector<bridge::AlgorithmDescriptorValue>& descriptor_values,
  bridge::AlgorithmObject* out_group,
  std::string* out_error_message,
  bool load_reflector);

bool QueryAlgorithmRequestedBindingsForManager(
  const std::string& algorithm_name,
  bridge::AlgorithmRequestedResources* out_requested_resources,
  bridge::AlgorithmRequestedDescriptorBindings* out_requested_descriptor_bindings,
  std::string* out_error_message);

void SetAlgorithmRuntimeShutdownHook();

void UnregisterMountedPipelineObjectsForManager(
  const std::vector<bridge::AlgorithmObject>& objects,
  const std::string& agent_name);

void UnregisterMountedPipelineObjectForManager(
  const bridge::AlgorithmObject& object,
  const std::string& agent_name);

void UnregisterMountedPipeline(
  const std::string& pipeline_name,
  const std::string& agent_name);

bool EnqueueMountedPipelineStage0SubmissionForManager(
  std::vector<bridge::AlgorithmObject>* algorithm_objects,
  const std::string& pipeline_name,
  const std::string& agent_name,
  const std::vector<bridge::AlgorithmResourceBinding>& resource_bindings,
  const std::vector<bridge::AlgorithmDescriptorValue>& descriptor_values,
  std::vector<bridge::AlgorithmAssemblyState>* algorithm_assembly_states,
  std::string* out_error_message,
  bool load_reflector);

bool EnqueueMountedPipelineStage0SubmissionNodeForManager(
  bridge::AlgorithmObject* pipeline_node,
  std::vector<bridge::AlgorithmAssemblyState>* inout_assembly_states,
  const std::string& agent_name,
  const std::vector<bridge::AlgorithmResourceBinding>& resource_bindings,
  const std::vector<bridge::AlgorithmDescriptorValue>& descriptor_values,
  std::string* out_error_message,
  bool load_reflector);

bool MountPipelineAlgorithmObjectsForManager(
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
  size_t* out_index,
  std::string* out_error_message,
  bool load_reflector);

bool ReplayMountedPipelineDebugForManager(
  std::vector<bridge::AlgorithmObject>* algorithm_objects,
  size_t index,
  const bridge::AgentTickContext& context,
  std::vector<bridge::AgentAlgorithmRuntimeState>* algorithm_runtime_states,
  std::string* out_error_message);

bool SubmitAlgorithmObjectForManager(
  const bridge::AlgorithmObject& object,
  const bridge::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  algorithm::AlgorithmContainerSet* container_set,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  bridge::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message);

bool TickMountedPipelineForManager(
  std::vector<bridge::AlgorithmObject>* algorithm_objects,
  size_t begin_index,
  size_t end_index,
  const std::string& agent_name,
  const bridge::AgentTickContext& context,
  const std::vector<bool>& allow_tick_mask,
  const std::vector<bridge::AlgorithmAssemblyState>& assembly_states,
  bool collect_timing_log,
  std::vector<bridge::AgentAlgorithmRuntimeState>* out_updated_runtime_states,
  common_data::AlgorithmToAgentSignal* out_pipeline_signal,
  bool* out_mounted_pipeline_processing_failed,
  std::string* out_error_message);

bool ReplayMountedPipelineDebugNodeForManager(
  bridge::AlgorithmObject* pipeline_node,
  bridge::AgentAlgorithmRuntimeState* inout_runtime_state,
  size_t child_index,
  const bridge::AgentTickContext& context,
  std::string* out_error_message);

bool TickMountedPipelineNodeForManager(
  bridge::AlgorithmObject* pipeline_node,
  bridge::AgentAlgorithmRuntimeState* inout_runtime_state,
  const std::string& agent_name,
  const bridge::AgentTickContext& context,
  bool allow_tick,
  const bridge::AlgorithmAssemblyState& assembly_state,
  bool collect_timing_log,
  common_data::AlgorithmToAgentSignal* out_pipeline_signal,
  bool* out_mounted_pipeline_processing_failed,
  std::string* out_error_message);

bool TryGetMountedPipelineRuntimeForManager(
  const std::string& pipeline_name,
  const std::string& agent_name,
  bridge::JobsPipelineRuntimeState* out_runtime_state);

bool TryGetMountedPipelineRegistrationForManager(
  const std::string& pipeline_name,
  bridge::JobsPipelineRegistration* out_registration);

} }  // namespace algomanager::algoscheduler
