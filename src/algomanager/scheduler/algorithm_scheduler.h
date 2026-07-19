#pragma once

#include <chrono>
#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "algomanager/bridge/algorithm_execution_bridge.h"
#include "algomanager/bridge/algorithm_manifest_bridge.h"
#include "algomanager/bridge/algorithm_pipeline_bridge.h"
#include "algomanager/bridge/algorithm_package_location.h"
#include "algomanager/bridge/algorithm_library_paths.h"
#include "common_data/kernel_cfg.h"
#define RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE 1
#include "runtimesys/runtime_environment.h"
#undef RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE

namespace algomanager { namespace algoscheduler {

void ClearAlgorithmScheduler();
void SetAlgorithmRuntimeShutdownHook();
void ClearAlgorithmExecutionCaches();
bool ExecuteJobsAlgorithmObject(
  const ::algomanager::algoscheduler::AlgorithmObject& object,
  const ::algomanager::algoscheduler::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  ::algorithm::AlgorithmContainerSet* container_set,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  ::algomanager::algoscheduler::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message = nullptr);
inline bool ExecuteCudaAlgorithmObject(
  const ::algomanager::algoscheduler::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  const ::algomanager::algoscheduler::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  ::algomanager::algoscheduler::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message = nullptr);
bool ExecuteVkAlgorithmObject(
  const ::algomanager::algoscheduler::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  const ::algomanager::algoscheduler::AgentTickContext& context,
  std::string* out_error_message = nullptr);
inline bool HasExecutableCudaAlgorithmStage(const ::algomanager::algoscheduler::AlgorithmObject& object);
bool HasExecutableVkAlgorithmStage(const ::algomanager::algoscheduler::AlgorithmObject& object);
inline bool SynchronizeCudaAlgorithmObject(
  const ::algomanager::algoscheduler::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message = nullptr);
bool SynchronizeVkAlgorithmObject(
  const ::algomanager::algoscheduler::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message = nullptr);

bool ExecuteAlgorithmObjectStagePlan(
  const ::algomanager::algoscheduler::AlgorithmObject& object,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  ::algorithm::AlgorithmContainerSet* container_set,
  const ::algomanager::algoscheduler::AgentTickContext& context,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  ::algomanager::algoscheduler::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message = nullptr);

namespace pipeline_scheduler_detail {

inline bool AreExecutionPreferencesCompatible(
  ::algomanager::bridge::AlgorithmExecutionPreference lhs,
  ::algomanager::bridge::AlgorithmExecutionPreference rhs) {
  return lhs == rhs;
}

bool HasPipelineStageBufferSlot(
  const algorithm::AlgorithmContainerSet& container_set,
  const std::string& stage_buffer_slot_name);

bool ValidatePipelineCircularLoopback(
  const algorithm::AlgorithmContainerSet& source_container_set,
  uint32_t shared_variable_count,
  uint32_t shared_array_count,
  ::algomanager::algoscheduler::AlgorithmObject* target_stage,
  std::string* out_error_message);

bool ValidateWholeStandardContainerLayout(
  const algorithm::AlgorithmContainerSet& source_container_set,
  algorithm::AlgorithmContainerSet* target_container_set,
  std::string* out_error_message);

bool BuildPipelineStageContainerSets(
  const std::vector<::algomanager::algoscheduler::AlgorithmObject>& algorithm_objects,
  size_t begin_index,
  size_t end_index,
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* out_stage_container_sets,
  std::string* out_error_message);

inline bool HasPipelineStageBufferSlot(
  const algorithm::AlgorithmContainerSet& container_set,
  const std::string& stage_buffer_slot_name) {
  if (stage_buffer_slot_name.empty()) {
    return false;
  }
  const algorithm::AlgorithmContainer* stage_buffer_container =
    algorithm::FindAlgorithmContainer(container_set, stage_buffer_slot_name);
  return stage_buffer_container != nullptr &&
    algorithm::IsStandardContainerSlotName(container_set, stage_buffer_slot_name);
}

inline bool ValidatePipelineCircularLoopback(
  const algorithm::AlgorithmContainerSet& source_container_set,
  uint32_t shared_variable_count,
  uint32_t shared_array_count,
  ::algomanager::algoscheduler::AlgorithmObject* target_stage,
  std::string* out_error_message) {
  if (!target_stage || !target_stage->mutable_container_set()) {
    if (out_error_message) {
      *out_error_message = "Circular pipeline loopback target container set is unavailable.";
    }
    return false;
  }
  algorithm::AlgorithmContainerSet* target_container_set = target_stage->mutable_container_set();
  if (!source_container_set.standard_layout.enabled() ||
      !target_container_set->standard_layout.enabled()) {
    if (out_error_message) {
      *out_error_message = "Circular pipeline loopback requires standard container layouts.";
    }
    return false;
  }
  if (source_container_set.standard_layout.variable_count < shared_variable_count ||
      target_container_set->standard_layout.variable_count < shared_variable_count ||
      source_container_set.standard_layout.array_count < shared_array_count ||
      target_container_set->standard_layout.array_count < shared_array_count) {
    if (out_error_message) {
      *out_error_message = "Circular pipeline loopback shared prefix is out of range.";
    }
    return false;
  }
  for (uint32_t i = 0; i < shared_variable_count; ++i) {
    const std::string slot_name = source_container_set.standard_layout.MakeVariableName(i);
    const algorithm::AlgorithmContainer* source_container =
      algorithm::FindAlgorithmContainer(source_container_set, slot_name);
    algorithm::AlgorithmContainer* target_container =
      algorithm::FindAlgorithmContainer(target_container_set, slot_name);
    if (!source_container || !target_container) {
      if (out_error_message) {
        *out_error_message = "Circular pipeline loopback is missing standard variable slot '" + slot_name + "'.";
      }
      return false;
    }
    if (!algorithm::HasSameContainerStructure(*source_container, *target_container)) {
      if (out_error_message) {
        *out_error_message = "Circular pipeline loopback variable slot structure mismatch at '" + slot_name + "'.";
      }
      return false;
    }
  }
  for (uint32_t i = 0; i < shared_array_count; ++i) {
    const std::string slot_name = source_container_set.standard_layout.MakeArrayName(i);
    const algorithm::AlgorithmContainer* source_container =
      algorithm::FindAlgorithmContainer(source_container_set, slot_name);
    algorithm::AlgorithmContainer* target_container =
      algorithm::FindAlgorithmContainer(target_container_set, slot_name);
    if (!source_container || !target_container) {
      if (out_error_message) {
        *out_error_message = "Circular pipeline loopback is missing standard array slot '" + slot_name + "'.";
      }
      return false;
    }
    if (!algorithm::HasSameContainerStructure(*source_container, *target_container)) {
      if (out_error_message) {
        *out_error_message = "Circular pipeline loopback array slot structure mismatch at '" + slot_name + "'.";
      }
      return false;
    }
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

inline bool ValidateWholeStandardContainerLayout(
  const algorithm::AlgorithmContainerSet& source_container_set,
  algorithm::AlgorithmContainerSet* target_container_set,
  std::string* out_error_message) {
  if (!target_container_set) {
    if (out_error_message) {
      *out_error_message = "Pipeline wrapper standard container target is unavailable.";
    }
    return false;
  }
  if (!source_container_set.standard_layout.enabled() ||
      !target_container_set->standard_layout.enabled()) {
    if (out_error_message) {
      *out_error_message = "Pipeline wrapper standard container copy requires standard layouts.";
    }
    return false;
  }
  if (source_container_set.standard_layout.variable_count != target_container_set->standard_layout.variable_count ||
      source_container_set.standard_layout.array_count != target_container_set->standard_layout.array_count) {
    if (out_error_message) {
      *out_error_message = "Pipeline wrapper standard container layout count mismatch.";
    }
    return false;
  }
  for (uint32_t i = 0u; i < source_container_set.standard_layout.variable_count; ++i) {
    const std::string slot_name = source_container_set.standard_layout.MakeVariableName(i);
    const algorithm::AlgorithmContainer* source_container =
      algorithm::FindAlgorithmContainer(source_container_set, slot_name);
    algorithm::AlgorithmContainer* target_container =
      algorithm::FindAlgorithmContainer(target_container_set, slot_name);
    if (!source_container || !target_container) {
      if (out_error_message) {
        *out_error_message = "Pipeline wrapper copy is missing standard variable slot '" + slot_name + "'.";
      }
      return false;
    }
    if (!algorithm::HasSameContainerStructure(*source_container, *target_container)) {
      if (out_error_message) {
        *out_error_message = "Pipeline wrapper variable slot structure mismatch at '" + slot_name + "'.";
      }
      return false;
    }
  }
  for (uint32_t i = 0u; i < source_container_set.standard_layout.array_count; ++i) {
    const std::string slot_name = source_container_set.standard_layout.MakeArrayName(i);
    const algorithm::AlgorithmContainer* source_container =
      algorithm::FindAlgorithmContainer(source_container_set, slot_name);
    algorithm::AlgorithmContainer* target_container =
      algorithm::FindAlgorithmContainer(target_container_set, slot_name);
    if (!source_container || !target_container) {
      if (out_error_message) {
        *out_error_message = "Pipeline wrapper copy is missing standard array slot '" + slot_name + "'.";
      }
      return false;
    }
    if (!algorithm::HasSameContainerStructure(*source_container, *target_container)) {
      if (out_error_message) {
        *out_error_message = "Pipeline wrapper array slot structure mismatch at '" + slot_name + "'.";
      }
      return false;
    }
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

inline bool BuildPipelineStageContainerSets(
  const std::vector<::algomanager::algoscheduler::AlgorithmObject>& algorithm_objects,
  size_t begin_index,
  size_t end_index,
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* out_stage_container_sets,
  std::string* out_error_message) {
  if (!out_stage_container_sets) {
    if (out_error_message) {
      *out_error_message = "Pipeline stage container set output pointer is null.";
    }
    return false;
  }
  out_stage_container_sets->clear();
  out_stage_container_sets->reserve(end_index > begin_index ? end_index - begin_index : 0u);
  for (size_t index = begin_index; index < end_index; ++index) {
    const ::algomanager::algoscheduler::AlgorithmObject& object = algorithm_objects[index];
    if (object.algorithm_profile.algorithm_name.empty()) {
      if (out_error_message) {
        *out_error_message = "Pipeline stage name is empty.";
      }
      return false;
    }
    if (!object.container_set()) {
      if (out_error_message) {
        *out_error_message = "Pipeline stage container set is unavailable for '" + object.algorithm_profile.algorithm_name + "'.";
      }
      return false;
    }
    const auto [_, inserted] = out_stage_container_sets->emplace(
      object.algorithm_profile.algorithm_name,
      object.shared_container_set);
    if (!inserted) {
      if (out_error_message) {
        *out_error_message = "Pipeline stage name is duplicated: '" + object.algorithm_profile.algorithm_name + "'.";
      }
      return false;
    }
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

}  // namespace pipeline_scheduler_detail

using ::algorithm::AlgorithmContainer;
using ::algorithm::AlgorithmContainerSet;
using ::algorithm::AlgorithmContainerStorageKind;
using ::algorithm::AlgorithmPackageLocation;
using ::algorithm::AlgorithmProfile;
using ::algorithm::AlgorithmReflectionBinding;
using ::algorithm::AlgorithmReflector;
using ::algomanager::bridge::AlgorithmPipelineStageSubmission;
using ::algorithm::AlgorithmStandardContainerLayout;
using ::algorithm::FindAlgorithmContainer;
using ::algorithm::TryResolveAlgorithmPackageLocation;

class AlgorithmScheduler {
 public:
  static AlgorithmScheduler& Instance();

  bool SubmitAlgorithmObject(
    const ::algomanager::algoscheduler::AlgorithmObject& object,
    const ::algomanager::algoscheduler::AgentTickContext& context,
    const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
    ::algorithm::AlgorithmContainerSet* container_set,
    common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
    ::algomanager::algoscheduler::AlgorithmPackageDebugState* out_debug_state,
    std::string* out_error_message = nullptr);

  bool RegisterPipeline(
    const JobsPipelineRegistration& registration,
    std::string* out_error_message = nullptr);

  bool RegisterPipelineRuntime(
    const std::string& pipeline_name,
    const std::string& owner_agent_name,
    const JobsPipelineRuntimeState& runtime_state,
    std::string* out_error_message = nullptr);

  bool EnqueuePipelineStage0Submission(
    const std::string& pipeline_name,
    const std::string& owner_agent_name,
    const std::string& stage0_algorithm_name,
    const std::vector<AlgorithmResourceBinding>& resource_bindings,
    const std::vector<AlgorithmDescriptorValue>& descriptor_values,
    std::string* out_error_message = nullptr,
    bool load_reflector = true);

  bool MountPipelineAlgorithmObjects(
    std::vector<::algomanager::bridge::AlgorithmObject>* mounted_objects,
    std::vector<::algomanager::bridge::AgentAlgorithmRuntimeState>* inout_runtime_states,
    std::vector<::algomanager::bridge::AlgorithmAssemblyState>* inout_assembly_states,
    std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* standard_shared_container_sets,
    const std::string& owner_agent_name,
    const std::string& pipeline_name,
    const std::vector<AlgorithmPipelineStageSubmission>& stage_submissions,
    AlgorithmExecutionPreference execution_preference,
    AlgorithmPipelineTopology topology,
    AlgorithmPipelineSyncMode sync_mode,
    size_t* out_begin_index = nullptr,
    std::string* out_error_message = nullptr,
    bool load_reflector = true);

  bool EnqueueMountedPipelineStage0Submission(
    std::vector<::algomanager::bridge::AlgorithmObject>* mounted_objects,
    const std::string& pipeline_name,
    const std::string& owner_agent_name,
    const std::vector<AlgorithmResourceBinding>& resource_bindings,
    const std::vector<AlgorithmDescriptorValue>& descriptor_values,
    std::vector<::algomanager::bridge::AlgorithmAssemblyState>* inout_assembly_states,
    std::string* out_error_message = nullptr,
    bool load_reflector = true);

  bool EnqueueMountedPipelineStage0SubmissionNode(
    ::algomanager::bridge::AlgorithmObject* pipeline_node,
    std::vector<::algomanager::bridge::AlgorithmAssemblyState>* inout_assembly_states,
    const std::string& owner_agent_name,
    const std::vector<AlgorithmResourceBinding>& resource_bindings,
    const std::vector<AlgorithmDescriptorValue>& descriptor_values,
    std::string* out_error_message = nullptr,
    bool load_reflector = true);

  bool TickMountedPipeline(
    std::vector<::algomanager::bridge::AlgorithmObject>* mounted_objects,
    size_t begin_index,
    size_t end_index,
    const std::string& owner_agent_name,
    const ::algomanager::algoscheduler::AgentTickContext& context,
    const std::vector<bool>& allow_tick_mask,
    const std::vector<::algomanager::bridge::AlgorithmAssemblyState>& assembly_states,
    bool collect_pipeline_timing,
    std::vector<::algomanager::bridge::AgentAlgorithmRuntimeState>* inout_runtime_states,
    common_data::AlgorithmToAgentSignal* out_pipeline_signal,
    bool* out_pipeline_processing_failed,
    std::string* out_error_message = nullptr);

  bool TickMountedPipelineNode(
    ::algomanager::bridge::AlgorithmObject* pipeline_node,
    ::algomanager::bridge::AgentAlgorithmRuntimeState* inout_runtime_state,
    const std::string& owner_agent_name,
    const ::algomanager::algoscheduler::AgentTickContext& context,
    bool allow_tick,
    const ::algomanager::bridge::AlgorithmAssemblyState& assembly_state,
    bool collect_pipeline_timing,
    common_data::AlgorithmToAgentSignal* out_pipeline_signal,
    bool* out_pipeline_processing_failed,
    std::string* out_error_message = nullptr);

  bool ReplayMountedPipelineDebug(
    std::vector<::algomanager::bridge::AlgorithmObject>* mounted_objects,
    size_t index,
    const ::algomanager::algoscheduler::AgentTickContext& context,
    std::vector<::algomanager::bridge::AgentAlgorithmRuntimeState>* inout_runtime_states,
    std::string* out_error_message = nullptr);

  bool ReplayMountedPipelineDebugNode(
    ::algomanager::bridge::AlgorithmObject* pipeline_node,
    ::algomanager::bridge::AgentAlgorithmRuntimeState* inout_runtime_state,
    size_t child_index,
    const ::algomanager::algoscheduler::AgentTickContext& context,
    std::string* out_error_message = nullptr);

  void UnregisterPipeline(const std::string& pipeline_name, const std::string& owner_agent_name);

  bool TryGetPipelineRegistration(
    const std::string& pipeline_name,
    JobsPipelineRegistration* out_registration) const;

  bool TryGetPipelineRuntime(
    const std::string& pipeline_name,
    const std::string& owner_agent_name,
    JobsPipelineRuntimeState* out_runtime_state) const;

  bool UpdatePipelineRuntime(
    const std::string& pipeline_name,
    const std::string& owner_agent_name,
    const JobsPipelineRuntimeState& runtime_state,
    std::string* out_error_message = nullptr);

  void BeginDebugToolRecording();
  void EndDebugToolRecording();
  bool DebugToolRecording() const;
  uint64_t DebugToolRecordingTickCount() const;
  void RecordDebugToolTick();

  void Clear();

 private:
  AlgorithmScheduler() = default;

  mutable std::mutex mutex_{};
  std::unordered_map<std::string, JobsPipelineRegistration> pipeline_registrations_{};
  // Shared pipeline runtime storage keyed by pipeline name.
  std::unordered_map<std::string, std::unordered_map<std::string, JobsPipelineRuntimeState>> pipeline_runtime_states_{};
  // Reference count for shared pipeline names across agents.
  std::unordered_map<std::string, std::unordered_map<std::string, size_t>> pipeline_runtime_ref_counts_{};
  bool debug_tool_recording_{false};
  uint64_t debug_tool_recording_tick_count_{0u};
};

inline bool CreateAlgorithmObjectFromLocation(
  const ::algorithm::AlgorithmPackageLocation& package_location,
  ::algomanager::algoscheduler::AlgorithmObject* out_group,
  std::string* out_error_message = nullptr,
  bool load_reflector = true) {
  return algomanager::bridge::CreateAlgorithmObjectFromLocation(
    package_location,
    out_group,
    out_error_message,
    load_reflector);
}

inline bool QueryAlgorithmRequestedBindings(
  const std::string& algorithm_name,
  AlgorithmRequestedResources* out_requested_resources,
  AlgorithmRequestedDescriptorBindings* out_requested_descriptor_bindings,
  std::string* out_error_message = nullptr) {
  if (!out_requested_resources || !out_requested_descriptor_bindings) {
    if (out_error_message) {
      *out_error_message = "Requested binding output pointers are null.";
    }
    return false;
  }

  out_requested_resources->algorithm_name = algorithm_name;
  out_requested_resources->required_resources.clear();
  out_requested_resources->valid = false;
  out_requested_descriptor_bindings->algorithm_name = algorithm_name;
  out_requested_descriptor_bindings->descriptor_slots.clear();
  out_requested_descriptor_bindings->valid = false;

  ::algorithm::AlgorithmPackageLocation package_location{};
  std::string location_error_message;
  if (!TryResolveAlgorithmPackageLocation(
        algorithm_name,
        &package_location,
        &location_error_message)) {
    if (out_error_message) {
      *out_error_message = location_error_message.empty()
        ? ("Failed to resolve algorithm package location for '" + algorithm_name + "'.")
        : std::move(location_error_message);
    }
    return false;
  }

  return algomanager::bridge::QueryAlgorithmPackageRequestedBindingsFromLocation(
    package_location,
    out_requested_resources,
    out_requested_descriptor_bindings,
    out_error_message);
}

inline bool LoadAlgorithmPackageDefaultBindings(
  const std::string& algorithm_name,
  std::vector<AlgorithmResourceBinding>* out_resource_bindings,
  std::vector<AlgorithmDescriptorValue>* out_descriptor_values,
  bool* out_has_default_file = nullptr,
  std::string* out_error_message = nullptr) {
  if (!out_resource_bindings || !out_descriptor_values) {
    if (out_error_message) {
      *out_error_message = "Default binding output pointers are null.";
    }
    return false;
  }

  out_resource_bindings->clear();
  out_descriptor_values->clear();
  if (out_has_default_file) {
    *out_has_default_file = false;
  }

  ::algorithm::AlgorithmPackageLocation package_location{};
  std::string location_error_message;
  if (!TryResolveAlgorithmPackageLocation(
        algorithm_name,
        &package_location,
        &location_error_message)) {
    if (out_error_message) {
      *out_error_message = location_error_message.empty()
        ? ("Failed to resolve algorithm package location for '" + algorithm_name + "'.")
        : std::move(location_error_message);
    }
    return false;
  }

  return algomanager::bridge::LoadAlgorithmPackageDefaultBindingsFromLocation(
    package_location,
    out_resource_bindings,
    out_descriptor_values,
    out_has_default_file,
    out_error_message);
}

inline bool LoadAlgorithmPackageDefaultBindingsFromLocation(
  const ::algorithm::AlgorithmPackageLocation& package_location,
  std::vector<AlgorithmResourceBinding>* out_resource_bindings,
  std::vector<AlgorithmDescriptorValue>* out_descriptor_values,
  bool* out_has_default_file = nullptr,
  std::string* out_error_message = nullptr) {
  return algomanager::bridge::LoadAlgorithmPackageDefaultBindingsFromLocation(
    package_location,
    out_resource_bindings,
    out_descriptor_values,
    out_has_default_file,
    out_error_message);
}

inline bool FinalizeAlgorithmObject(
  const ::algomanager::algoscheduler::AlgorithmObject& algorithm_object,
  ::algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message = nullptr) {
  if (!container_set) {
    if (out_error_message) {
      *out_error_message = "AlgorithmContainerSet output pointer is null.";
    }
    return false;
  }

  ::algorithm::AlgorithmPackageLocation package_location{};
  std::string location_error_message;
  if (!TryResolveAlgorithmPackageLocation(
        algorithm_object.algorithm_profile.algorithm_name,
        &package_location,
        &location_error_message)) {
    if (out_error_message) {
      *out_error_message = location_error_message.empty()
        ? ("Failed to resolve algorithm package location for '" + algorithm_object.algorithm_profile.algorithm_name + "'.")
        : std::move(location_error_message);
    }
    return false;
  }

  const bool ok = algomanager::bridge::DecomposeAlgorithmPackageFromLocation(
    package_location,
    algorithm_object.resource_bindings,
    algorithm_object.descriptor_values,
    container_set,
    out_error_message);
  if (ok && out_error_message) {
    out_error_message->clear();
  }
  return ok;
}

inline bool PrepareAlgorithmObjectByName(
  const std::string& algorithm_name,
  const std::vector<AlgorithmResourceBinding>& resource_bindings,
  const std::vector<AlgorithmDescriptorValue>& descriptor_values,
  ::algomanager::algoscheduler::AlgorithmObject* out_object,
  std::string* out_error_message = nullptr,
  bool load_reflector = true) {
  if (!out_object) {
    if (out_error_message) {
      *out_error_message = "Prepared algorithm object output pointer is null.";
    }
    return false;
  }

  ::algorithm::AlgorithmPackageLocation package_location{};
  std::string location_error_message;
  if (!TryResolveAlgorithmPackageLocation(
        algorithm_name,
        &package_location,
        &location_error_message)) {
    if (out_error_message) {
      *out_error_message = location_error_message.empty()
        ? ("Failed to resolve algorithm package location for '" + algorithm_name + "'.")
        : std::move(location_error_message);
    }
    return false;
  }

  ::algomanager::algoscheduler::AlgorithmObject prepared_object{};
  std::string create_error_message;
  if (!algomanager::bridge::CreateAlgorithmObjectFromLocation(
        package_location,
        &prepared_object,
        &create_error_message,
        load_reflector)) {
    if (out_error_message) {
      *out_error_message = create_error_message.empty()
        ? ("Failed to create algorithm object for '" + algorithm_name + "'.")
        : std::move(create_error_message);
    }
    return false;
  }

  prepared_object.resource_bindings = resource_bindings;
  prepared_object.descriptor_values = descriptor_values;
  if (!prepared_object.mutable_container_set()) {
    if (out_error_message) {
      *out_error_message = "Prepared algorithm container set is unavailable.";
    }
    return false;
  }

  std::string finalize_error_message;
  if (!FinalizeAlgorithmObject(
        prepared_object,
        prepared_object.mutable_container_set(),
        &finalize_error_message)) {
    if (out_error_message) {
      *out_error_message = finalize_error_message.empty()
        ? ("Failed to finalize prepared algorithm object for '" + algorithm_name + "'.")
        : std::move(finalize_error_message);
    }
    return false;
  }

  *out_object = std::move(prepared_object);
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

namespace pipeline_scheduler_detail {

#ifndef NDEBUG
#define ALGORITHM_SCHEDULER_ASSERT(condition, message) do { \
  if (!(condition)) { \
    std::cerr << (message) << '\n'; \
    assert((condition) && (message)); \
  } \
} while (false)
#else
#define ALGORITHM_SCHEDULER_ASSERT(condition, message) ((void)0)
#endif

using PipelineRuntimeState = JobsPipelineRuntimeState;
using PipelineLaneRuntimeState = JobsPipelineLaneRuntimeState;
using PendingPipelineStage0Submission = JobsPendingPipelineStage0Submission;
using PipelineInterStageBufferRuntimeState = JobsPipelineInterStageBufferRuntimeState;

constexpr float kPipelineNoProgressStallSeconds = 1.0f;
constexpr uint64_t kFnvOffsetBasis64 = 1469598103934665603ull;
constexpr uint64_t kFnvPrime64 = 1099511628211ull;

inline uint64_t HashBytes(uint64_t hash, const void* data, size_t size) {
  const auto* bytes = static_cast<const unsigned char*>(data);
  for (size_t i = 0; i < size; ++i) {
    hash ^= static_cast<uint64_t>(bytes[i]);
    hash *= kFnvPrime64;
  }
  return hash;
}

inline uint64_t HashString(uint64_t hash, const std::string& value) {
  hash = HashBytes(hash, value.data(), value.size());
  const char terminator = '\0';
  return HashBytes(hash, &terminator, sizeof(terminator));
}

inline uint64_t HashU64(uint64_t hash, uint64_t value) {
  return HashBytes(hash, &value, sizeof(value));
}

inline uint64_t HashBool(uint64_t hash, bool value) {
  const uint8_t byte_value = value ? 1u : 0u;
  return HashBytes(hash, &byte_value, sizeof(byte_value));
}

inline uint64_t HashSignal(uint64_t hash, const common_data::AlgorithmToAgentSignal& signal) {
  hash = HashBool(hash, signal.intervention_applied);
  hash = HashBool(hash, signal.pause_requested);
  hash = HashBool(hash, signal.stop_requested);
  hash = HashBool(hash, signal.intervention_needed);
  hash = HashBool(hash, signal.reflection_collection_requested);
  hash = HashU64(hash, static_cast<uint64_t>(signal.control_bits));
  return hash;
}

inline uint64_t HashSignal(uint64_t hash, const common_data::AgentToAlgorithmSignal& signal) {
  hash = HashBool(hash, signal.needs_intervention);
  hash = HashBool(hash, signal.pause_requested);
  hash = HashBool(hash, signal.stop_requested);
  hash = HashBool(hash, signal.reflection_collection_requested);
  hash = HashU64(hash, static_cast<uint64_t>(signal.control_bits));
  return hash;
}

inline uint64_t HashContainer(uint64_t hash, const algorithm::AlgorithmContainer& container) {
  hash = HashString(hash, container.name);
  hash = HashU64(hash, static_cast<uint64_t>(container.storage_kind));
  hash = HashU64(hash, static_cast<uint64_t>(container.element_count));
  hash = HashU64(hash, static_cast<uint64_t>(container.element_stride));
  hash = HashBool(hash, container.hidden);
  hash = HashU64(hash, static_cast<uint64_t>(container.bytes.size()));
  if (!container.bytes.empty()) {
    hash = HashBytes(hash, container.bytes.data(), container.bytes.size());
  }
  return hash;
}

inline uint64_t HashContainerSet(uint64_t hash, const algorithm::AlgorithmContainerSet& container_set) {
  hash = HashString(hash, container_set.algorithm_name);
  hash = HashString(hash, container_set.standard_layout.layout_name);
  hash = HashString(hash, container_set.standard_layout.layout_kind);
  hash = HashU64(hash, static_cast<uint64_t>(container_set.standard_layout.variable_count));
  hash = HashU64(hash, static_cast<uint64_t>(container_set.standard_layout.array_count));
  hash = HashString(hash, container_set.standard_layout.variable_prefix);
  hash = HashString(hash, container_set.standard_layout.array_prefix);
  for (const auto& container : container_set.arrays) {
    hash = HashContainer(hash, container);
  }
  for (const auto& container : container_set.temporary_registers) {
    hash = HashContainer(hash, container);
  }
  for (const auto& container : container_set.temporary_caches) {
    hash = HashContainer(hash, container);
  }
  for (const auto& container : container_set.hidden_containers) {
    hash = HashContainer(hash, container);
  }
  return hash;
}

inline uint64_t HashPipelineGroupState(
  const std::vector<::algomanager::bridge::AlgorithmObject>& algorithm_objects,
  const std::vector<size_t>& executable_indices,
  uint64_t current_lane_id,
  size_t valid_lane_count,
  const std::vector<bool>& stage_has_data,
  size_t pending_stage0_submission_count,
  bool stage0_saturated,
  const std::vector<::algomanager::bridge::AgentAlgorithmRuntimeState>& runtime_states,
  size_t begin_index,
  size_t end_index) {
  uint64_t hash = kFnvOffsetBasis64;
  for (size_t index = begin_index; index < end_index; ++index) {
    const ::algomanager::bridge::AlgorithmObject& object = algorithm_objects[index];
    hash = HashString(hash, object.algorithm_profile.algorithm_name);
    hash = HashU64(hash, static_cast<uint64_t>(object.pipeline_stage_index));
    hash = HashU64(hash, static_cast<uint64_t>(object.pipeline_stage_count));
    const algorithm::AlgorithmContainerSet* container_set = object.container_set();
    if (container_set) {
      hash = HashContainerSet(hash, *container_set);
    }
    if (container_set) {
      hash = HashU64(
        hash,
        runtimesys::RuntimeVkContextRegistry::Instance().SnapshotExecutionProgress(container_set));
    }
    if (index < runtime_states.size()) {
      hash = HashSignal(hash, runtime_states[index].agent_to_algorithm_signal);
      hash = HashSignal(hash, runtime_states[index].algorithm_to_agent_signal);
    }
  }
  for (bool has_data : stage_has_data) {
    hash = HashBool(hash, has_data);
  }
  hash = HashU64(hash, current_lane_id);
  hash = HashU64(hash, static_cast<uint64_t>(valid_lane_count));
  hash = HashU64(hash, static_cast<uint64_t>(pending_stage0_submission_count));
  hash = HashBool(hash, stage0_saturated);
  for (size_t index : executable_indices) {
    hash = HashU64(hash, static_cast<uint64_t>(index));
  }
  return hash;
}

struct PipelineGroupProgressState {
  uint64_t signature{0u};
  bool signature_valid{false};
  float no_progress_seconds{0.0f};
  bool stall_report_requested{false};
  bool stall_reported{false};
  std::string stall_reason;
  float total_elapsed_seconds{0.0f};
  std::vector<AlgorithmPipelineStageRuntimeStat> stage_runtime_stats;
};

inline AlgorithmPipelineStageRuntimeStat* FindPipelineStageRuntimeStat(
  std::vector<AlgorithmPipelineStageRuntimeStat>* stage_runtime_stats,
  const std::string& stage_name) {
  if (!stage_runtime_stats) {
    return nullptr;
  }
  for (AlgorithmPipelineStageRuntimeStat& stage_stat : *stage_runtime_stats) {
    if (stage_stat.stage_name == stage_name) {
      return &stage_stat;
    }
  }
  return nullptr;
}

inline void SetPipelineStageRuntimeReason(
  std::vector<AlgorithmPipelineStageRuntimeStat>* stage_runtime_stats,
  const std::string& stage_name,
  std::string reason) {
  AlgorithmPipelineStageRuntimeStat* stage_stat =
    FindPipelineStageRuntimeStat(stage_runtime_stats, stage_name);
  if (!stage_stat) {
    return;
  }
  stage_stat->reason = std::move(reason);
}

inline void AddPipelineStageRuntimeElapsed(
  std::vector<AlgorithmPipelineStageRuntimeStat>* stage_runtime_stats,
  const std::string& stage_name,
  float elapsed_seconds) {
  AlgorithmPipelineStageRuntimeStat* stage_stat =
    FindPipelineStageRuntimeStat(stage_runtime_stats, stage_name);
  if (!stage_stat) {
    return;
  }
  stage_stat->elapsed_seconds += elapsed_seconds;
}

inline std::string BuildPipelineStageIdleReason(
  bool allow_tick,
  bool is_ready,
  bool launch_once_completed) {
  if (!allow_tick) {
    return "Tick was skipped by the scheduler.";
  }
  if (!is_ready) {
    return "Stage assembly is not ready.";
  }
  if (launch_once_completed) {
    return "Launch-once stage is holding its completed state.";
  }
  return "Stage did not execute.";
}

inline std::string BuildPipelineStageNoInputReason(bool is_stage0, bool has_pending_stage0_submission) {
  if (is_stage0 && has_pending_stage0_submission) {
    return "Stage0 is waiting for a free execution slot before accepting the next resource batch.";
  }
  if (is_stage0) {
    return "Stage0 is waiting for an external resource batch.";
  }
  return "Stage has no pipeline input token.";
}

inline void CollectDebugState(
  const ::algomanager::algoscheduler::AlgorithmObject& object,
  ::algomanager::algoscheduler::AlgorithmPackageDebugState* out_debug_state) {
  if (out_debug_state) {
    *out_debug_state = {};
  }
  if (auto* complex_support = dynamic_cast<::algomanager::bridge::IComplexAlgorithmPackageSupport*>(object.reflector.get())) {
    if (out_debug_state) {
      complex_support->CollectDebugState(out_debug_state);
    }
  }
}

inline bool CollectReflectionSnapshot(
  const ::algomanager::algoscheduler::AlgorithmObject& object,
  const AlgorithmContainerSet& container_set,
  AlgorithmReflectionSnapshot* out_snapshot) {
  if (!out_snapshot) {
    return false;
  }
  out_snapshot->Clear();
  out_snapshot->algorithm_name = object.algorithm_profile.algorithm_name;
  if (!object.algorithm_reflector) {
    return false;
  }
  for (const auto& [reflection_object_name, binding] : object.algorithm_reflector->container_bindings_by_reflection_object_name) {
    for (const std::string& container_name : binding.container_names) {
      const AlgorithmContainer* container = FindAlgorithmContainer(container_set, container_name);
      if (!container) {
        continue;
      }
      AlgorithmReflectionValue value{};
      value.reflection_object_name = reflection_object_name;
      value.container_name = container_name;
      value.filter_name = binding.filter_name;
      value.storage_kind = container->storage_kind;
      value.bytes.assign(container->bytes.begin(), container->bytes.end());
      if (container->storage_kind == algorithm::AlgorithmContainerStorageKind::Array) {
        out_snapshot->variable_arrays.push_back(std::move(value));
      } else {
        out_snapshot->variables.push_back(std::move(value));
      }
    }
  }
  out_snapshot->valid = !out_snapshot->variables.empty() || !out_snapshot->variable_arrays.empty();
  return out_snapshot->valid;
}

inline bool CollectPipelineExitReflectionSnapshot(
  const std::string& algorithm_name,
  const AlgorithmContainerSet& container_set,
  AlgorithmReflectionSnapshot* out_snapshot) {
  if (!out_snapshot) {
    return false;
  }
  out_snapshot->Clear();
  out_snapshot->algorithm_name = algorithm_name.empty() ? container_set.algorithm_name : algorithm_name;
  for (const AlgorithmContainer& container : container_set.arrays) {
    AlgorithmReflectionValue value{};
    value.reflection_object_name = container.name;
    value.container_name = container.name;
    value.filter_name = "pipeline_exit";
    value.storage_kind = container.storage_kind;
    value.bytes.assign(container.bytes.begin(), container.bytes.end());
    out_snapshot->variable_arrays.push_back(std::move(value));
  }
  for (const AlgorithmContainer& container : container_set.temporary_registers) {
    AlgorithmReflectionValue value{};
    value.reflection_object_name = container.name;
    value.container_name = container.name;
    value.filter_name = "pipeline_exit";
    value.storage_kind = container.storage_kind;
    value.bytes.assign(container.bytes.begin(), container.bytes.end());
    out_snapshot->variables.push_back(std::move(value));
  }
  for (const AlgorithmContainer& container : container_set.temporary_caches) {
    AlgorithmReflectionValue value{};
    value.reflection_object_name = container.name;
    value.container_name = container.name;
    value.filter_name = "pipeline_exit";
    value.storage_kind = container.storage_kind;
    value.bytes.assign(container.bytes.begin(), container.bytes.end());
    out_snapshot->variables.push_back(std::move(value));
  }
  for (const AlgorithmContainer& container : container_set.hidden_containers) {
    AlgorithmReflectionValue value{};
    value.reflection_object_name = container.name;
    value.container_name = container.name;
    value.filter_name = "pipeline_exit";
    value.storage_kind = container.storage_kind;
    value.bytes.assign(container.bytes.begin(), container.bytes.end());
    out_snapshot->variables.push_back(std::move(value));
  }
  out_snapshot->valid = !out_snapshot->variables.empty() || !out_snapshot->variable_arrays.empty();
  return out_snapshot->valid;
}

inline bool SignalBlocksTick(
  const ::algomanager::algoscheduler::AlgorithmObject& object,
  const common_data::AgentToAlgorithmSignal& signal) {
  if (signal.stop_requested || signal.pause_requested) {
    return true;
  }
  if (!signal.needs_intervention) {
    return false;
  }
  return !object.intervention || object.intervention->SupportsIntervention();
}

inline void UpdatePipelineGroupProgressState(
  const PipelineGroupProgressState& previous_state,
  uint64_t current_signature,
  bool signature_valid,
  float dt_seconds,
  PipelineGroupProgressState* out_state) {
  if (!out_state) {
    return;
  }
  *out_state = previous_state;
  out_state->total_elapsed_seconds = previous_state.total_elapsed_seconds + dt_seconds;
  out_state->stall_report_requested = false;
  if (!signature_valid) {
    return;
  }
  if (previous_state.signature_valid && previous_state.signature == current_signature) {
    out_state->signature = current_signature;
    out_state->signature_valid = true;
    out_state->no_progress_seconds = previous_state.no_progress_seconds + dt_seconds;
    if (!previous_state.stall_reported && out_state->no_progress_seconds >= kPipelineNoProgressStallSeconds) {
      out_state->stall_report_requested = true;
      out_state->stall_reported = true;
    }
    return;
  }
  out_state->signature = current_signature;
  out_state->signature_valid = true;
  out_state->no_progress_seconds = 0.0f;
  out_state->stall_reported = false;
  out_state->stall_reason.clear();
}

inline void ResetRuntimeStateBase(
  const AgentAlgorithmRuntimeState& previous_runtime_state,
  AgentAlgorithmRuntimeState* runtime_state) {
  if (!runtime_state) {
    return;
  }
  *runtime_state = previous_runtime_state;
  runtime_state->algorithm_to_agent_signal = {};
  runtime_state->debug_state = {};
  runtime_state->bridge_debug_set.Clear();
  if (!runtime_state->reflection_snapshot_cached) {
    runtime_state->reflection_snapshot.Clear();
  }
}

inline bool PipelineGroupIsExecutable(const std::vector<size_t>& executable_indices) {
  return !executable_indices.empty();
}

struct PipelineStageExecutionBundle {
  size_t begin_stage_offset{0u};
  size_t end_stage_offset{0u};
  AlgorithmExecutionPreference execution_preference{AlgorithmExecutionPreference::Vk};
};

inline size_t CountLivePipelineStages(const std::vector<bool>& stage_has_data) {
  size_t live_stage_count = 0u;
  for (bool has_data : stage_has_data) {
    if (has_data) {
      ++live_stage_count;
    }
  }
  return live_stage_count;
}

inline size_t CountValidPipelineLanes(const PipelineRuntimeState& pipeline_state) {
  size_t valid_lane_count = 0u;
  for (const PipelineLaneRuntimeState& lane_state : pipeline_state.lanes) {
    if (lane_state.valid) {
      ++valid_lane_count;
    }
  }
  return valid_lane_count;
}

struct BuiltAlgorithmMount {
  ::algomanager::bridge::AlgorithmObject object{};
  std::shared_ptr<algorithm::AlgorithmContainerSet> container_set{};
  std::string error_message;
  bool ok{false};
};

class NoOpPipelineWrapperJobsExecutor final : public ::algomanager::bridge::IAlgorithmJobsExecutor {
 public:
  bool ExecuteJobsAlgorithm(
    const AgentTickContext& context,
    const algorithm::AlgorithmProfile& algorithm_profile,
    const AgentToAlgorithmSignal& agent_to_algorithm_signal,
    algorithm::AlgorithmContainerSet* algorithm_container_set,
    AlgorithmToAgentSignal* algorithm_to_agent_signal,
    AlgorithmPackageDebugState* debug_state) override {
    (void)context;
    (void)algorithm_profile;
    (void)agent_to_algorithm_signal;
    (void)algorithm_container_set;
    (void)algorithm_to_agent_signal;
    (void)debug_state;
    return true;
  }
};

inline bool AddPipelineStandardContainerAlias(
  algorithm::AlgorithmContainerSet* standard_container_set,
  const std::string& alias_name,
  const std::vector<std::string>& standard_slot_names,
  std::string* out_error_message) {
  if (algorithm::IsStandardContainerSlotName(*standard_container_set, alias_name)) {
    if (standard_slot_names.size() == 1u && standard_slot_names.front() == alias_name) {
      return true;
    }
    if (out_error_message) {
      *out_error_message = "Pipeline standard container alias conflicts with a standard slot: " + alias_name;
    }
    return false;
  }

  const auto found = standard_container_set->container_aliases_by_name.find(alias_name);
  if (found == standard_container_set->container_aliases_by_name.end()) {
    standard_container_set->container_aliases_by_name.emplace(alias_name, standard_slot_names);
    return true;
  }
  if (found->second == standard_slot_names) {
    return true;
  }
  if (out_error_message) {
    *out_error_message = "Pipeline standard container alias maps to different slots: " + alias_name;
  }
  return false;
}

inline bool CollectPipelineStandardAliasTable(
  const algorithm::AlgorithmContainerSet& stage_container_set,
  std::unordered_map<std::string, std::vector<std::string>>* out_alias_table,
  std::string* out_error_message) {
  out_alias_table->clear();
  std::unordered_map<std::string, std::string> direct_name_to_slot{};

  const auto collect_direct = [&](
    const std::vector<algorithm::AlgorithmContainer>& containers,
    bool array_kind) {
    for (size_t index = 0u; index < containers.size(); ++index) {
      const std::string slot_name = array_kind
        ? stage_container_set.standard_layout.MakeArrayName(static_cast<uint32_t>(index))
        : stage_container_set.standard_layout.MakeVariableName(static_cast<uint32_t>(index));
      direct_name_to_slot.emplace(containers[index].name, slot_name);
      direct_name_to_slot.emplace(slot_name, slot_name);
    }
  };

  collect_direct(stage_container_set.arrays, true);
  collect_direct(stage_container_set.temporary_registers, false);

  for (const auto& [name, slot_name] : direct_name_to_slot) {
    if (name != slot_name) {
      (*out_alias_table)[name] = {slot_name};
    }
  }

  for (const auto& [alias_name, source_names] : stage_container_set.container_aliases_by_name) {
    std::vector<std::string> slot_names{};
    slot_names.reserve(source_names.size());
    for (const std::string& source_name : source_names) {
      const auto direct_found = direct_name_to_slot.find(source_name);
      if (direct_found != direct_name_to_slot.end()) {
        slot_names.push_back(direct_found->second);
        continue;
      }
      if (algorithm::IsStandardContainerSlotName(stage_container_set, source_name)) {
        slot_names.push_back(source_name);
        continue;
      }
      if (out_error_message) {
        *out_error_message = "Pipeline stage alias source is not mapped to a standard slot: " + source_name;
      }
      return false;
    }
    (*out_alias_table)[alias_name] = std::move(slot_names);
  }
  return true;
}

inline bool NormalizePipelineStandardContainerSet(
  algorithm::AlgorithmContainerSet* standard_container_set,
  std::string* out_error_message) {
  std::unordered_map<std::string, std::vector<std::string>> alias_table{};
  if (!CollectPipelineStandardAliasTable(*standard_container_set, &alias_table, out_error_message)) {
    return false;
  }

  for (size_t index = 0u; index < standard_container_set->arrays.size(); ++index) {
    standard_container_set->arrays[index].name =
      standard_container_set->standard_layout.MakeArrayName(static_cast<uint32_t>(index));
  }
  for (size_t index = 0u; index < standard_container_set->temporary_registers.size(); ++index) {
    standard_container_set->temporary_registers[index].name =
      standard_container_set->standard_layout.MakeVariableName(static_cast<uint32_t>(index));
  }

  standard_container_set->container_aliases_by_name.clear();
  for (const auto& [alias_name, slot_names] : alias_table) {
    if (!AddPipelineStandardContainerAlias(
          standard_container_set,
          alias_name,
          slot_names,
          out_error_message)) {
      return false;
    }
  }
  return true;
}

inline bool BindPipelineStageContainerSetToStandardContainerSet(
  const algorithm::AlgorithmContainerSet& stage_container_set,
  algorithm::AlgorithmContainerSet* standard_container_set,
  std::string* out_error_message) {
  std::unordered_map<std::string, std::vector<std::string>> alias_table{};
  if (!CollectPipelineStandardAliasTable(stage_container_set, &alias_table, out_error_message)) {
    return false;
  }
  for (const auto& [alias_name, slot_names] : alias_table) {
    if (!AddPipelineStandardContainerAlias(
          standard_container_set,
          alias_name,
          slot_names,
          out_error_message)) {
      return false;
    }
  }
  return true;
}

inline std::string StandardLayoutKey(const algorithm::AlgorithmContainerSet& container_set) {
  if (container_set.standard_layout.enabled()) {
    return container_set.standard_layout.layout_name;
  }
  return container_set.algorithm_name;
}

inline BuiltAlgorithmMount BuildAlgorithmMount(
  const std::string& algorithm_name,
  const std::vector<AlgorithmResourceBinding>& resource_bindings,
  const std::vector<AlgorithmDescriptorValue>& descriptor_values,
  ::algomanager::bridge::AlgorithmMountMode mount_mode,
  ::algomanager::bridge::AlgorithmExecutionPreference execution_preference,
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* standard_shared_container_sets) {
  BuiltAlgorithmMount result{};

  std::string location_error_message;
  ::algorithm::AlgorithmPackageLocation package_location{};
  if (!TryResolveAlgorithmPackageLocation(
        algorithm_name,
        &package_location,
        &location_error_message)) {
    result.error_message = location_error_message.empty()
      ? ("Failed to resolve algorithm package location for '" + algorithm_name + "'.")
      : std::move(location_error_message);
    return result;
  }

  ::algomanager::bridge::AlgorithmObject object{};
  std::string create_error_message;
  if (!algomanager::bridge::CreateAlgorithmObjectFromLocation(package_location, &object, &create_error_message)) {
    result.error_message = create_error_message.empty()
      ? ("Failed to create algorithm object for '" + algorithm_name + "'.")
      : std::move(create_error_message);
    return result;
  }

  object.resource_bindings = resource_bindings;
  object.descriptor_values = descriptor_values;
  object.mount_mode = mount_mode;
  object.execution_preference = execution_preference;

  std::shared_ptr<algorithm::AlgorithmContainerSet> container_set_handle = object.shared_container_set;
  if (!container_set_handle) {
    result.error_message = "Algorithm containers are unavailable for '" + algorithm_name + "'.";
    return result;
  }

  const bool use_shared_standard_container =
    mount_mode == ::algomanager::bridge::AlgorithmMountMode::StandardContainer &&
    container_set_handle->standard_layout.enabled() &&
    standard_shared_container_sets != nullptr;
  const bool cache_shared_standard_container = use_shared_standard_container;
  std::string shared_key{};

  if (use_shared_standard_container) {
    shared_key = StandardLayoutKey(*container_set_handle);
    if (!shared_key.empty()) {
      auto found = standard_shared_container_sets->find(shared_key);
      if (found != standard_shared_container_sets->end() && found->second) {
        container_set_handle = found->second;
      }
    }
  }

  std::string finalize_error_message;
  if (!FinalizeAlgorithmObject(
        object,
        container_set_handle.get(),
        &finalize_error_message)) {
    result.error_message = finalize_error_message.empty()
      ? ("Failed to finalize algorithm inputs for '" + algorithm_name + "'.")
      : std::move(finalize_error_message);
    return result;
  }

  if (cache_shared_standard_container && !shared_key.empty()) {
    (*standard_shared_container_sets)[shared_key] = container_set_handle;
  }

  object.shared_container_set = container_set_handle;
  result.object = std::move(object);
  result.container_set = std::move(container_set_handle);
  result.ok = true;
  return result;
}

inline BuiltAlgorithmMount BuildEmptyPipelineWrapperMount(
  const ::algomanager::bridge::AlgorithmObject& body_stage0_object,
  const std::string& wrapper_algorithm_name,
  ::algomanager::bridge::AlgorithmPipelineWrapperRole wrapper_role,
  ::algomanager::bridge::AlgorithmExecutionPreference execution_preference) {
  BuiltAlgorithmMount result{};
  if (!body_stage0_object.container_set()) {
    result.error_message = "Pipeline body stage0 container set is unavailable for wrapper mount.";
    return result;
  }

  result.object.algorithm_profile.algorithm_name = wrapper_algorithm_name;
  result.object.algorithm_profile.container_manifest_name = wrapper_algorithm_name;
  result.object.mount_mode = ::algomanager::bridge::AlgorithmMountMode::Pipeline;
  result.object.execution_preference = ::algomanager::bridge::AlgorithmExecutionPreference::Jobs;
  result.object.jobs_symbol = true;
  result.object.vk_symbol = false;
  result.object.cuda_symbol = false;
  result.object.jobs_executor = std::make_shared<NoOpPipelineWrapperJobsExecutor>();
  result.object.pipeline_wrapper_role = wrapper_role;
  result.object.pipeline_wrapper_empty = true;
  result.object.shared_container_set = body_stage0_object.shared_container_set;
  result.container_set = result.object.shared_container_set;
  result.ok = true;
  return result;
}

inline std::string DefaultPipelineWrapperAlgorithmName(
  const std::string& root_algorithm_name,
  ::algomanager::bridge::AlgorithmPipelineWrapperRole wrapper_role) {
  const char* suffix = wrapper_role == ::algomanager::bridge::AlgorithmPipelineWrapperRole::Begin
    ? "_stageBegin"
    : "_stageEnd";
  return root_algorithm_name + suffix;
}

inline BuiltAlgorithmMount BuildPipelineWrapperMount(
  const ::algorithm::AlgorithmPackageLocation& root_package_location,
  const ::algomanager::algoscheduler::AlgorithmObject& body_stage0_object,
  const algomanager::bridge::AlgorithmPipelineWrapperStageSpec& stage_spec,
  ::algomanager::bridge::AlgorithmPipelineWrapperRole wrapper_role,
  ::algomanager::bridge::AlgorithmExecutionPreference execution_preference,
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* standard_shared_container_sets) {
  const std::string root_algorithm_name = root_package_location.algorithm_name.empty()
    ? root_package_location.manifest_name
    : root_package_location.algorithm_name;
  std::string wrapper_algorithm_name = stage_spec.algorithm_name;
  if (wrapper_algorithm_name.empty()) {
    wrapper_algorithm_name = DefaultPipelineWrapperAlgorithmName(root_algorithm_name, wrapper_role);
  }

  ::algorithm::AlgorithmPackageLocation wrapper_package_location{};
  if (!TryResolveAlgorithmPackageLocation(wrapper_algorithm_name, &wrapper_package_location, nullptr)) {
    BuiltAlgorithmMount result;
    result.ok = false;
    result.error_message = "Pipeline wrapper stage package cannot be resolved: " + wrapper_algorithm_name;
    return result;
  }

  BuiltAlgorithmMount result = BuildAlgorithmMount(
    wrapper_algorithm_name,
    {},
    {},
    ::algomanager::bridge::AlgorithmMountMode::Pipeline,
    execution_preference,
    standard_shared_container_sets);
  if (!result.ok) {
    return result;
  }
  result.object.pipeline_wrapper_role = wrapper_role;
  result.object.pipeline_wrapper_empty = false;
  return result;
}

inline bool RuntimeTransferMapsMatch(
  const algorithm::AlgorithmRuntimeTransferMap& expected_map,
  const algorithm::AlgorithmRuntimeTransferMap& candidate_map,
  std::string* out_error_message) {
  if (expected_map.supports_circular_tick != candidate_map.supports_circular_tick) {
    if (out_error_message) {
      *out_error_message = "Pipeline stage runtime transfer map circular tick support does not match.";
    }
    return false;
  }
  if (expected_map.stage_links.size() != candidate_map.stage_links.size()) {
    if (out_error_message) {
      *out_error_message = "Pipeline stage runtime transfer map edge count does not match.";
    }
    return false;
  }
  for (size_t edge_index = 0u; edge_index < expected_map.stage_links.size(); ++edge_index) {
    const algorithm::AlgorithmRuntimeTransferEdge& expected_edge = expected_map.stage_links[edge_index];
    const algorithm::AlgorithmRuntimeTransferEdge& candidate_edge = candidate_map.stage_links[edge_index];
    if (expected_edge.source_stage_name != candidate_edge.source_stage_name ||
        expected_edge.target_stage_name != candidate_edge.target_stage_name) {
      if (out_error_message) {
        *out_error_message =
          "Pipeline stage runtime transfer map topology does not match at edge index " +
          std::to_string(edge_index) +
          ". expected=" + expected_edge.source_stage_name + "->" + expected_edge.target_stage_name +
          ", actual=" + candidate_edge.source_stage_name + "->" + candidate_edge.target_stage_name + ".";
      }
      return false;
    }
    if (expected_edge.bindings.size() != candidate_edge.bindings.size()) {
      if (out_error_message) {
        *out_error_message = "Pipeline stage runtime transfer map binding count does not match.";
      }
      return false;
    }
    for (size_t binding_index = 0u; binding_index < expected_edge.bindings.size(); ++binding_index) {
      const algorithm::AlgorithmRuntimeTransferBinding& expected_binding = expected_edge.bindings[binding_index];
      const algorithm::AlgorithmRuntimeTransferBinding& candidate_binding = candidate_edge.bindings[binding_index];
      if (expected_binding.from_name != candidate_binding.from_name ||
          expected_binding.to_name != candidate_binding.to_name ||
          expected_binding.required != candidate_binding.required) {
        if (out_error_message) {
          *out_error_message = "Pipeline stage runtime transfer map binding content does not match.";
        }
        return false;
      }
    }
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

inline bool ValidatePipelineSharedStandardPrefix(
  const std::vector<BuiltAlgorithmMount>& built_stages,
  uint32_t shared_variable_count,
  uint32_t shared_array_count,
  std::string* out_error_message) {
  if (built_stages.empty() || !built_stages.front().container_set) {
    if (out_error_message) {
      *out_error_message = "Pipeline stage container set list is empty.";
    }
    return false;
  }

  const algorithm::AlgorithmContainerSet& reference_container_set = *built_stages.front().container_set;
  for (size_t stage_index = 0u; stage_index < built_stages.size(); ++stage_index) {
    const BuiltAlgorithmMount& built_stage = built_stages[stage_index];
    if (!built_stage.container_set) {
      if (out_error_message) {
        *out_error_message = "Pipeline stage container set is unavailable.";
      }
      return false;
    }

    const algorithm::AlgorithmContainerSet& candidate_container_set = *built_stage.container_set;
    for (uint32_t variable_index = 0u; variable_index < shared_variable_count; ++variable_index) {
      const std::string slot_name = reference_container_set.standard_layout.MakeVariableName(variable_index);
      const algorithm::AlgorithmContainer* reference_container =
        algorithm::FindAlgorithmContainer(reference_container_set, slot_name);
      const algorithm::AlgorithmContainer* candidate_container =
        algorithm::FindAlgorithmContainer(candidate_container_set, slot_name);
      if (!reference_container || !candidate_container) {
        if (out_error_message) {
          *out_error_message = "Pipeline shared standard variable slot is missing: '" + slot_name +
            "' in stage '" + built_stage.object.algorithm_profile.algorithm_name + "'.";
        }
        return false;
      }
      if (!algorithm::HasSameContainerStructure(*reference_container, *candidate_container)) {
        if (out_error_message) {
          *out_error_message = "Pipeline shared standard variable slot structure mismatch at '" + slot_name +
            "' in stage '" + built_stage.object.algorithm_profile.algorithm_name + "'.";
        }
        return false;
      }
    }

    for (uint32_t array_index = 0u; array_index < shared_array_count; ++array_index) {
      const std::string slot_name = reference_container_set.standard_layout.MakeArrayName(array_index);
      const algorithm::AlgorithmContainer* reference_container =
        algorithm::FindAlgorithmContainer(reference_container_set, slot_name);
      const algorithm::AlgorithmContainer* candidate_container =
        algorithm::FindAlgorithmContainer(candidate_container_set, slot_name);
      if (!reference_container || !candidate_container) {
        if (out_error_message) {
          *out_error_message = "Pipeline shared standard array slot is missing: '" + slot_name +
            "' in stage '" + built_stage.object.algorithm_profile.algorithm_name + "'.";
        }
        return false;
      }
      if (!algorithm::HasSameContainerStructure(*reference_container, *candidate_container)) {
        if (out_error_message) {
          *out_error_message = "Pipeline shared standard array slot structure mismatch at '" + slot_name +
            "' in stage '" + built_stage.object.algorithm_profile.algorithm_name + "'.";
        }
        return false;
      }
    }
  }

  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

inline bool TryBuildMountedPipelineTransferMap(
  const std::vector<BuiltAlgorithmMount>& built_stages,
  std::shared_ptr<algorithm::AlgorithmRuntimeTransferMap>* out_transfer_map,
  std::string* out_error_message) {
  if (!out_transfer_map) {
    if (out_error_message) {
      *out_error_message = "Mounted pipeline runtime transfer map output pointer is null.";
    }
    return false;
  }
  out_transfer_map->reset();
  if (built_stages.empty()) {
    if (out_error_message) {
      *out_error_message = "Pipeline stage mount list is empty.";
    }
    return false;
  }
  if (!built_stages.front().object.runtime_transfer_map ||
      !built_stages.front().object.runtime_transfer_map->valid) {
    if (out_error_message) {
      *out_error_message = "Pipeline stage runtime transfer map is unavailable on the first stage.";
    }
    return false;
  }

  algorithm::AlgorithmRuntimeTransferMap transfer_map = *built_stages.front().object.runtime_transfer_map;
  uint32_t shared_variable_count = UINT32_MAX;
  uint32_t shared_array_count = UINT32_MAX;
  uint32_t total_extra_variable_count = 0u;
  uint32_t total_extra_array_count = 0u;

  for (const BuiltAlgorithmMount& built_stage : built_stages) {
    if (!built_stage.container_set || !built_stage.container_set->standard_layout.enabled()) {
      if (out_error_message) {
        *out_error_message = "Pipeline stage standard container layout is unavailable: " +
          built_stage.object.algorithm_profile.algorithm_name;
      }
      return false;
    }
    if (!built_stage.object.runtime_transfer_map || !built_stage.object.runtime_transfer_map->valid) {
      if (out_error_message) {
        *out_error_message = "Pipeline stage runtime transfer map is unavailable: " +
          built_stage.object.algorithm_profile.algorithm_name;
      }
      return false;
    }
    std::string map_compare_error;
    if (!RuntimeTransferMapsMatch(
          transfer_map,
          *built_stage.object.runtime_transfer_map,
          &map_compare_error)) {
      if (out_error_message) {
        *out_error_message = map_compare_error.empty()
          ? ("Pipeline stage runtime transfer map mismatch: " +
             built_stage.object.algorithm_profile.algorithm_name)
          : (map_compare_error + " Stage: " + built_stage.object.algorithm_profile.algorithm_name);
      }
      return false;
    }

    shared_variable_count = std::min(shared_variable_count, built_stage.container_set->standard_layout.variable_count);
    shared_array_count = std::min(shared_array_count, built_stage.container_set->standard_layout.array_count);
  }

  if (shared_variable_count == UINT32_MAX) {
    shared_variable_count = 0u;
  }
  if (shared_array_count == UINT32_MAX) {
    shared_array_count = 0u;
  }
  if (shared_array_count == 0u) {
    if (out_error_message) {
      *out_error_message =
        "Pipeline stages must share at least one standard array slot for the implicit stage buffer.";
    }
    return false;
  }

  std::string prefix_validation_error;
  if (!ValidatePipelineSharedStandardPrefix(
        built_stages,
        shared_variable_count,
        shared_array_count,
        &prefix_validation_error)) {
    if (out_error_message) {
      *out_error_message = prefix_validation_error;
    }
    return false;
  }

  transfer_map.stage_layouts.clear();
  transfer_map.stage_layouts.reserve(built_stages.size());
  for (const BuiltAlgorithmMount& built_stage : built_stages) {
    const algorithm::AlgorithmStandardContainerLayout& standard_layout = built_stage.container_set->standard_layout;
    const uint32_t extra_variable_count = standard_layout.variable_count - shared_variable_count;
    const uint32_t extra_array_count = standard_layout.array_count - shared_array_count;
    if (extra_array_count != 0u) {
      if (out_error_message) {
        *out_error_message =
          "Pipeline stage is not allowed to declare extra standard arrays beyond the shared prefix: " +
          built_stage.object.algorithm_profile.algorithm_name;
      }
      return false;
    }

    transfer_map.stage_layouts.push_back(algorithm::AlgorithmRuntimeTransferStageLayout{
      .stage_name = built_stage.object.algorithm_profile.algorithm_name,
      .declared_variable_count = standard_layout.variable_count,
      .declared_array_count = standard_layout.array_count,
      .shared_variable_count = shared_variable_count,
      .shared_array_count = shared_array_count,
      .extra_variable_count = extra_variable_count,
      .extra_array_count = extra_array_count,
      .extra_variable_offset = total_extra_variable_count,
      .extra_array_offset = total_extra_array_count,
    });
    total_extra_variable_count += extra_variable_count;
    total_extra_array_count += extra_array_count;
  }

  transfer_map.pipeline_shared_variable_count = shared_variable_count;
  transfer_map.pipeline_shared_array_count = shared_array_count;
  transfer_map.pipeline_total_extra_variable_count = total_extra_variable_count;
  transfer_map.pipeline_total_extra_array_count = total_extra_array_count;
  transfer_map.pipeline_shared_stage_buffer_slot_name =
    built_stages.front().container_set->standard_layout.MakeArrayName(shared_array_count - 1u);
  const algorithm::AlgorithmContainer* stage_buffer_container =
    algorithm::FindAlgorithmContainer(
      *built_stages.front().container_set,
      transfer_map.pipeline_shared_stage_buffer_slot_name);
  if (!stage_buffer_container) {
    if (out_error_message) {
      *out_error_message = "Pipeline shared stage buffer container is unavailable.";
    }
    return false;
  }
  if (stage_buffer_container->storage_kind != algorithm::AlgorithmContainerStorageKind::Array ||
      stage_buffer_container->element_stride < sizeof(float) ||
      stage_buffer_container->element_count < total_extra_variable_count) {
    if (out_error_message) {
      *out_error_message =
        "Pipeline shared stage buffer does not have enough scalar capacity for all extra stage variables.";
    }
    return false;
  }

  *out_transfer_map = std::make_shared<algorithm::AlgorithmRuntimeTransferMap>(std::move(transfer_map));
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

inline bool PipelineNameInUse(
  const std::vector<::algomanager::bridge::AlgorithmObject>& mounted_objects,
  const std::string& pipeline_name) {
  if (pipeline_name.empty()) {
    return false;
  }
  for (const ::algomanager::bridge::AlgorithmObject& object : mounted_objects) {
    if (object.pipeline_stage && object.pipeline_name == pipeline_name) {
      return true;
    }
  }
  return false;
}

inline bool FindMountedPipelineRange(
  const std::vector<::algomanager::bridge::AlgorithmObject>& mounted_objects,
  const std::string& pipeline_name,
  size_t* out_begin_index,
  size_t* out_end_index) {
  if (pipeline_name.empty()) {
    return false;
  }

  for (size_t index = 0u; index < mounted_objects.size(); ++index) {
    const ::algomanager::bridge::AlgorithmObject& anchor = mounted_objects[index];
    if (!anchor.pipeline_stage || anchor.pipeline_name != pipeline_name) {
      continue;
    }

    size_t begin_index = index;
    while (begin_index > 0u) {
      const ::algomanager::bridge::AlgorithmObject& previous = mounted_objects[begin_index - 1u];
      if (!previous.pipeline_stage ||
          previous.pipeline_name != pipeline_name ||
          previous.pipeline_stage_index + 1u != mounted_objects[begin_index].pipeline_stage_index) {
        break;
      }
      --begin_index;
    }

    size_t end_index = index + 1u;
    while (end_index < mounted_objects.size()) {
      const ::algomanager::bridge::AlgorithmObject& next = mounted_objects[end_index];
      if (!next.pipeline_stage ||
          next.pipeline_name != pipeline_name ||
          mounted_objects[end_index - 1u].pipeline_stage_index + 1u != next.pipeline_stage_index) {
        break;
      }
      ++end_index;
    }

    if (out_begin_index) {
      *out_begin_index = begin_index;
    }
    if (out_end_index) {
      *out_end_index = end_index;
    }
    return true;
  }

  return false;
}

inline bool InitializePipelineInterStageBufferRuntimeState(
  const ::algomanager::algoscheduler::AlgorithmObject& stage0_object,
  const std::string& stage_buffer_slot_name,
  PipelineInterStageBufferRuntimeState* out_inter_stage_buffer,
  std::string* out_error_message);

inline bool TryBuildInitialPipelineLaneRuntimeState(
  const ::algomanager::algoscheduler::AlgorithmObject& stage0_object,
  size_t pipeline_stage_count,
  const std::string& owner_agent_name,
  bool loop_lane_active,
  uint64_t lane_id,
  const std::string& stage_buffer_slot_name,
  PipelineLaneRuntimeState* out_lane_state,
  std::string* out_error_message) {
  if (!out_lane_state) {
    if (out_error_message) {
      *out_error_message = "Pipeline lane runtime state output pointer is null.";
    }
    return false;
  }
  const algorithm::AlgorithmContainerSet* standard_container_set = stage0_object.container_set();
  if (!standard_container_set) {
    if (out_error_message) {
      *out_error_message = "Pipeline stage0 standard container set is unavailable.";
    }
    return false;
  }
  if (!HasPipelineStageBufferSlot(*standard_container_set, stage_buffer_slot_name)) {
    if (out_error_message) {
      *out_error_message =
        "Pipeline stage0 standard container is missing the required pipeline stage buffer slot '" +
        stage_buffer_slot_name + "'.";
    }
    return false;
  }
  out_lane_state->owner_agent_name = owner_agent_name;
  out_lane_state->lane_id = lane_id;
  out_lane_state->loop_lane_active = loop_lane_active;
  out_lane_state->standard_container_set = stage0_object.shared_container_set;
  out_lane_state->resource_bindings = stage0_object.resource_bindings;
  out_lane_state->descriptor_values = stage0_object.descriptor_values;
  out_lane_state->stage_has_data.assign(pipeline_stage_count, false);
  if (!InitializePipelineInterStageBufferRuntimeState(
        stage0_object,
        stage_buffer_slot_name,
        &out_lane_state->inter_stage_buffer,
        out_error_message)) {
    return false;
  }
  out_lane_state->valid = true;
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

inline PipelineLaneRuntimeState* FindPrimaryPipelineLaneRuntimeState(PipelineRuntimeState* pipeline_state) {
  if (!pipeline_state) {
    return nullptr;
  }
  if (pipeline_state->current_lane_id != 0u) {
    for (PipelineLaneRuntimeState& lane_state : pipeline_state->lanes) {
      if (lane_state.valid && lane_state.lane_id == pipeline_state->current_lane_id) {
        return &lane_state;
      }
    }
  }
  for (PipelineLaneRuntimeState& lane_state : pipeline_state->lanes) {
    if (lane_state.valid) {
      return &lane_state;
    }
  }
  return nullptr;
}

inline const PipelineLaneRuntimeState* FindPrimaryPipelineLaneRuntimeState(const PipelineRuntimeState& pipeline_state) {
  if (pipeline_state.current_lane_id != 0u) {
    for (const PipelineLaneRuntimeState& lane_state : pipeline_state.lanes) {
      if (lane_state.valid && lane_state.lane_id == pipeline_state.current_lane_id) {
        return &lane_state;
      }
    }
  }
  for (const PipelineLaneRuntimeState& lane_state : pipeline_state.lanes) {
    if (lane_state.valid) {
      return &lane_state;
    }
  }
  return nullptr;
}

inline void SyncPipelineLegacyStageStateFromPrimaryLane(
  PipelineRuntimeState* pipeline_state,
  size_t pipeline_stage_count) {
  if (!pipeline_state) {
    return;
  }
  pipeline_state->stage_has_data.assign(pipeline_stage_count, false);
  const PipelineLaneRuntimeState* primary_lane = FindPrimaryPipelineLaneRuntimeState(*pipeline_state);
  if (!primary_lane) {
    return;
  }
  pipeline_state->stage_has_data = primary_lane->stage_has_data;
}

inline void CommitPipelineStageStateToPrimaryLane(
  PipelineRuntimeState* pipeline_state,
  const std::vector<bool>& next_stage_has_data) {
  if (!pipeline_state) {
    return;
  }
  PipelineLaneRuntimeState* primary_lane = FindPrimaryPipelineLaneRuntimeState(pipeline_state);
  if (primary_lane) {
    primary_lane->stage_has_data = next_stage_has_data;
    if (!next_stage_has_data.empty()) {
      primary_lane->loop_lane_active = next_stage_has_data.front();
    }
  }
  pipeline_state->stage_has_data = next_stage_has_data;
}

inline PipelineLaneRuntimeState* FindPipelineLaneRuntimeStateById(
  PipelineRuntimeState* pipeline_state,
  uint64_t lane_id) {
  if (!pipeline_state || lane_id == 0u) {
    return nullptr;
  }
  for (PipelineLaneRuntimeState& lane_state : pipeline_state->lanes) {
    if (lane_state.valid && lane_state.lane_id == lane_id) {
      return &lane_state;
    }
  }
  return nullptr;
}

inline void MergeAlgorithmToAgentSignal(
  const common_data::AlgorithmToAgentSignal& source_signal,
  common_data::AlgorithmToAgentSignal* out_target_signal) {
  if (!out_target_signal) {
    return;
  }
  out_target_signal->intervention_applied = out_target_signal->intervention_applied || source_signal.intervention_applied;
  out_target_signal->pause_requested = out_target_signal->pause_requested || source_signal.pause_requested;
  out_target_signal->stop_requested = out_target_signal->stop_requested || source_signal.stop_requested;
  out_target_signal->intervention_needed = out_target_signal->intervention_needed || source_signal.intervention_needed;
  out_target_signal->reflection_collection_requested =
    out_target_signal->reflection_collection_requested || source_signal.reflection_collection_requested;
  out_target_signal->control_bits |= source_signal.control_bits;
}

inline bool ClearPipelineExternalWriteResetContainers(
  const ::algomanager::algoscheduler::AlgorithmObject& object,
  algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message) {
  if (!container_set) {
    if (out_error_message) {
      *out_error_message = "Pipeline external-write reset target container set is unavailable.";
    }
    return false;
  }

  for (const std::string& container_name : object.pipeline_external_write_reset_container_names) {
    algorithm::AlgorithmContainer* container =
      algorithm::FindAlgorithmContainer(container_set, container_name);
    if (!container) {
      if (out_error_message) {
        *out_error_message =
          "Pipeline external-write reset container is missing: " +
          object.algorithm_profile.algorithm_name + "." + container_name;
      }
      return false;
    }
    if (!container->bytes.empty()) {
      std::memset(container->bytes.data(), 0, container->bytes.size());
    }
  }

  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

inline bool InitializePipelineInterStageBufferRuntimeState(
  const ::algomanager::algoscheduler::AlgorithmObject& stage0_object,
  const std::string& stage_buffer_slot_name,
  PipelineInterStageBufferRuntimeState* out_inter_stage_buffer,
  std::string* out_error_message) {
  if (!out_inter_stage_buffer) {
    if (out_error_message) {
      *out_error_message = "Pipeline inter-stage buffer runtime state output pointer is null.";
    }
    return false;
  }
  if (!stage0_object.runtime_transfer_map || !stage0_object.runtime_transfer_map->valid) {
    if (out_error_message) {
      *out_error_message = "Pipeline runtime transfer map is unavailable while building the inter-stage buffer.";
    }
    return false;
  }
  out_inter_stage_buffer->standard_container_slot_name = stage_buffer_slot_name;
  out_inter_stage_buffer->scalar_slot_count =
    stage0_object.runtime_transfer_map->pipeline_total_extra_variable_count;
  out_inter_stage_buffer->scalar_slots.assign(
    static_cast<size_t>(out_inter_stage_buffer->scalar_slot_count),
    0.0f);
  out_inter_stage_buffer->valid = true;
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

inline bool CollectAlgorithmRuntimeReflection(
  const ::algomanager::algoscheduler::AlgorithmObject& object,
  bool capture_reflection_once,
  AgentAlgorithmRuntimeState* runtime_state) {
  const bool has_runtime_reflector = object.algorithm_reflector && !object.algorithm_reflector->empty();
  if (has_runtime_reflector) {
    const bool should_collect_reflection =
      !capture_reflection_once || !runtime_state->reflection_snapshot_cached;
    if (!should_collect_reflection) {
      return true;
    }
    if (!object.container_set()) {
      ALGORITHM_SCHEDULER_ASSERT(false, "Runtime reflection snapshot requires a container set.");
      runtime_state->algorithm_to_agent_signal.stop_requested = true;
      return false;
    }
    if (!CollectReflectionSnapshot(
          object,
          *object.container_set(),
          &runtime_state->reflection_snapshot)) {
      ALGORITHM_SCHEDULER_ASSERT(false, "Runtime reflection snapshot could not be collected.");
      runtime_state->algorithm_to_agent_signal.stop_requested = true;
      return false;
    }
    runtime_state->algorithm_to_agent_signal.reflection_collection_requested = true;
    if (capture_reflection_once) {
      runtime_state->reflection_snapshot_cached = true;
    }
    return true;
  }
  if (runtime_state->agent_to_algorithm_signal.reflection_collection_requested) {
    ALGORITHM_SCHEDULER_ASSERT(false, "Reflection was requested but the algorithm has no runtime reflector.");
    runtime_state->algorithm_to_agent_signal.stop_requested = true;
    return false;
  }
  return true;
}

inline bool TickAlgorithmObject(
  ::algomanager::algoscheduler::AlgorithmObject& object,
  const ::algomanager::algoscheduler::AgentTickContext& context,
  bool allow_tick,
  bool is_ready,
  bool execute_now,
  AgentAlgorithmRuntimeState* runtime_state) {
  if (!runtime_state) {
    return false;
  }
  runtime_state->algorithm_to_agent_signal = {};
  runtime_state->debug_state = {};
  runtime_state->algorithm_exec_elapsed_seconds = 0.0f;
  runtime_state->algorithm_exec_elapsed_valid = false;
  const bool launch_once_then_hold = object.tick_lifetime == AlgorithmTickLifetime::LaunchOnceThenHold;
  const bool launch_once_completed = launch_once_then_hold && runtime_state->launch_once_completed;
  const bool has_runtime_reflector = object.algorithm_reflector && !object.algorithm_reflector->empty();
  const bool capture_reflection_once =
    has_runtime_reflector &&
    object.algorithm_reflector->refresh_mode == algorithm::AlgorithmReflectionRefreshMode::CaptureOnceAfterCompletion;
  const bool keep_cached_reflection = runtime_state->reflection_snapshot_cached;
  if (!keep_cached_reflection) {
    runtime_state->reflection_snapshot.Clear();
  }
  if (launch_once_completed) {
    runtime_state->agent_to_algorithm_signal = {};
    return true;
  }
  if (!execute_now) {
    if (is_ready) {
      runtime_state->algorithm_to_agent_signal.pause_requested =
        runtime_state->agent_to_algorithm_signal.pause_requested;
      runtime_state->algorithm_to_agent_signal.stop_requested =
        runtime_state->agent_to_algorithm_signal.stop_requested;
      runtime_state->algorithm_to_agent_signal.intervention_needed =
        SignalBlocksTick(object, runtime_state->agent_to_algorithm_signal) &&
        runtime_state->agent_to_algorithm_signal.needs_intervention;
      runtime_state->algorithm_to_agent_signal.reflection_collection_requested =
        runtime_state->agent_to_algorithm_signal.reflection_collection_requested;
      runtime_state->algorithm_to_agent_signal.control_bits =
        runtime_state->agent_to_algorithm_signal.control_bits;
    } else {
      runtime_state->agent_to_algorithm_signal = {};
      runtime_state->algorithm_to_agent_signal = {};
    }
    (void)allow_tick;
    return true;
  }
  runtime_state->algorithm_to_agent_signal.intervention_needed =
    runtime_state->agent_to_algorithm_signal.needs_intervention &&
    object.intervention &&
    object.intervention->SupportsIntervention();
  runtime_state->algorithm_to_agent_signal.control_bits =
    runtime_state->agent_to_algorithm_signal.control_bits;
  std::shared_ptr<algorithm::AlgorithmContainerSet> container_set_handle = object.shared_container_set;
  std::string submit_error_message;
  const auto exec_begin = std::chrono::steady_clock::now();
  const bool submit_ok = AlgorithmScheduler::Instance().SubmitAlgorithmObject(
        object,
        context,
        runtime_state->agent_to_algorithm_signal,
        container_set_handle.get(),
        &runtime_state->algorithm_to_agent_signal,
        &runtime_state->debug_state,
        &submit_error_message);
  runtime_state->algorithm_exec_elapsed_seconds =
    std::chrono::duration<float>(std::chrono::steady_clock::now() - exec_begin).count();
  runtime_state->algorithm_exec_elapsed_valid = true;
  if (!submit_ok) {
    const std::string failure_message = submit_error_message.empty()
      ? std::string("Algorithm execution failed.")
      : std::move(submit_error_message);
    std::cerr
      << "scheduler_submit_failed algorithm=" << object.algorithm_profile.algorithm_name
      << " pipeline=" << object.pipeline_name
      << " error=" << failure_message << '\n';
    ALGORITHM_SCHEDULER_ASSERT(false, failure_message.c_str());
    runtime_state->algorithm_to_agent_signal.stop_requested = true;
    return false;
  }
  AlgorithmPackageDebugState collected_debug_state{};
  CollectDebugState(object, &collected_debug_state);
  runtime_state->debug_state.signals.insert(
    runtime_state->debug_state.signals.end(),
    collected_debug_state.signals.begin(),
    collected_debug_state.signals.end());
  if (!CollectAlgorithmRuntimeReflection(
        object,
        capture_reflection_once,
        runtime_state)) {
    return false;
  }
  runtime_state->algorithm_to_agent_signal.intervention_applied =
    runtime_state->algorithm_to_agent_signal.intervention_applied ||
    runtime_state->algorithm_to_agent_signal.intervention_needed;
  if (!runtime_state->algorithm_to_agent_signal.stop_requested && launch_once_then_hold) {
    runtime_state->launch_once_completed = true;
    if (!has_runtime_reflector) {
      runtime_state->reflection_snapshot_cached = true;
    }
  }
  return true;
}

}  // namespace pipeline_scheduler_detail

inline IoBufferPacket BuildAlgorithmInterventionPacket(
  const algomanager::bridge::AlgorithmInterventionDescriptor& descriptor) {
  return algomanager::bridge::BuildAlgorithmInterventionPacket(descriptor);
}

inline bool DecodeAlgorithmInterventionPacket(
  const IoBufferPacket& packet,
  algomanager::bridge::DecodedAlgorithmIntervention* decoded) {
  return algomanager::bridge::DecodeAlgorithmInterventionPacket(packet, decoded);
}

inline IoBufferPacket BuildAlgorithmInterventionPacket(
  const InteractionInterventionRequest& request) {
  return algomanager::bridge::BuildAlgorithmInterventionPacket(request);
}

inline bool DecodeAlgorithmInterventionPacket(
  const IoBufferPacket& packet,
  InteractionInterventionRequest* request) {
  return algomanager::bridge::DecodeAlgorithmInterventionPacket(packet, request);
}

inline bool ExecuteCompatibilityAlgorithmObject(
  const ::algomanager::algoscheduler::AlgorithmObject& object,
  const ::algomanager::algoscheduler::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  ::algorithm::AlgorithmContainerSet* container_set,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  ::algomanager::algoscheduler::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message = nullptr) {
  if (!out_algorithm_to_agent_signal || !out_debug_state) {
    if (out_error_message) {
      *out_error_message = "Compatibility algorithm execution output pointer is null.";
    }
    return false;
  }
  if (!object.compatibility_executor) {
    if (out_error_message) {
      *out_error_message = "Compatibility algorithm executor is unavailable.";
    }
    return false;
  }

  return runtimesys::SubmitBlockingJob(
    context.job_priority == AlgorithmJobPriority::High
      ? runtimesys::RuntimeJobPriority::High
      : (context.job_priority == AlgorithmJobPriority::Normal
        ? runtimesys::RuntimeJobPriority::Normal
        : runtimesys::RuntimeJobPriority::Low),
    [&object, &context, &agent_to_algorithm_signal, container_set, out_algorithm_to_agent_signal, out_debug_state](
      std::string* out_job_error_message) {
      const AlgorithmCompatibilityContainerWriter container_writer{
        .context = container_set,
        .write_array_bytes = [](void* context, const char* container_name, const void* bytes, size_t byte_count) {
          algorithm::AlgorithmContainer* container = algorithm::FindAlgorithmContainer(
            static_cast<algorithm::AlgorithmContainerSet*>(context), container_name);
          std::memcpy(container->bytes.data(), bytes, byte_count);
        },
      };
      const bool ok = object.compatibility_executor->ExecuteCompatibleAlgorithm(
        context,
        object.algorithm_profile,
        agent_to_algorithm_signal,
        &container_writer,
        out_algorithm_to_agent_signal,
        out_debug_state);
      if (!ok && out_job_error_message) {
        *out_job_error_message = "Compatibility algorithm execution failed.";
      }
    },
    out_error_message);
}

inline void UnregisterMountedPipeline(
  const std::string& pipeline_name,
  const std::string& owner_agent_name) {
  AlgorithmScheduler::Instance().UnregisterPipeline(pipeline_name, owner_agent_name);
}

inline void UnregisterMountedPipelineObjects(
  const std::vector<::algomanager::bridge::AlgorithmObject>& objects,
  const std::string& owner_agent_name) {
  for (const ::algomanager::bridge::AlgorithmObject& object : objects) {
    if (!object.child_algorithm_objects.empty() && !object.pipeline_name.empty()) {
      UnregisterMountedPipeline(object.pipeline_name, owner_agent_name);
    }
  }
}

inline void UnregisterMountedPipelineObject(
  const ::algomanager::bridge::AlgorithmObject& object,
  const std::string& owner_agent_name) {
  if (!object.child_algorithm_objects.empty() && !object.pipeline_name.empty()) {
    UnregisterMountedPipeline(object.pipeline_name, owner_agent_name);
  }
}

inline bool AlgorithmScheduler::EnqueuePipelineStage0Submission(
  const std::string& pipeline_name,
  const std::string& owner_agent_name,
  const std::string& stage0_algorithm_name,
  const std::vector<AlgorithmResourceBinding>& resource_bindings,
  const std::vector<AlgorithmDescriptorValue>& descriptor_values,
  std::string* out_error_message,
  bool load_reflector) {
  JobsPipelineRegistration registration{};
  JobsPipelineRuntimeState pipeline_runtime_state{};
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto registration_found = pipeline_registrations_.find(pipeline_name);
    if (registration_found == pipeline_registrations_.end()) {
      if (out_error_message) {
        *out_error_message = "Mounted pipeline registration is unavailable.";
      }
      return false;
    }
    registration = registration_found->second;
    const auto found = pipeline_runtime_states_.find(pipeline_name);
    if (found == pipeline_runtime_states_.end()) {
      if (out_error_message) {
        *out_error_message = "Mounted pipeline runtime state is unavailable.";
      }
      return false;
    }
    const auto owner_found = found->second.find(owner_agent_name);
    if (owner_found == found->second.end()) {
      if (out_error_message) {
        *out_error_message = "Mounted pipeline runtime state is unavailable for the selected agent.";
      }
      return false;
    }
    pipeline_runtime_state = owner_found->second;
  }

  pipeline_runtime_state.max_concurrent_stage0_submissions =
    registration.max_concurrent_stage0_submissions;
  if (pipeline_runtime_state.stage_has_data.size() != registration.stage_count) {
    pipeline_runtime_state.stage_has_data.assign(registration.stage_count, false);
  }
  size_t valid_lane_count = 0u;
  for (const JobsPipelineLaneRuntimeState& lane_state : pipeline_runtime_state.lanes) {
    if (lane_state.valid) {
      ++valid_lane_count;
    }
  }
  if (valid_lane_count + pipeline_runtime_state.pending_stage0_submissions.size() >=
      static_cast<size_t>(pipeline_runtime_state.max_concurrent_stage0_submissions)) {
    pipeline_runtime_state.stage0_saturated = true;
    std::lock_guard<std::mutex> lock(mutex_);
    pipeline_runtime_states_[pipeline_name][owner_agent_name] = pipeline_runtime_state;
    if (out_error_message) {
      *out_error_message = "Pipeline stage0 is saturated and cannot accept more resource batches.";
    }
    return false;
  }
  pipeline_runtime_state.stage0_saturated = false;

  ::algomanager::algoscheduler::AlgorithmObject prepared_stage0_object{};
  std::string prepare_error_message;
  if (!PrepareAlgorithmObjectByName(
        stage0_algorithm_name,
        resource_bindings,
        descriptor_values,
        &prepared_stage0_object,
        &prepare_error_message,
        load_reflector)) {
    if (out_error_message) {
      *out_error_message = prepare_error_message.empty()
        ? "Failed to prepare the pipeline stage0 submission."
        : std::move(prepare_error_message);
    }
    return false;
  }
  const algorithm::AlgorithmContainerSet* prepared_container_set = prepared_stage0_object.container_set();
  if (!prepared_container_set ||
      !algorithm::HasMandatoryPipelineStageBuffer(*prepared_container_set) ||
      !algorithm::IsStandardContainerSlotName(
        *prepared_container_set,
        pipeline_runtime_state.mandatory_stage_buffer_slot_name)) {
    if (out_error_message) {
      *out_error_message =
        "Prepared stage0 submission is missing the required pipeline stage buffer slot '" +
        pipeline_runtime_state.mandatory_stage_buffer_slot_name + "'.";
    }
    return false;
  }

  std::string prepared_standard_mapping_error;
  if (!pipeline_scheduler_detail::NormalizePipelineStandardContainerSet(
        prepared_stage0_object.mutable_container_set(),
        &prepared_standard_mapping_error)) {
    if (out_error_message) {
      *out_error_message = prepared_standard_mapping_error.empty()
        ? "Failed to normalize the submitted pipeline standard container set."
        : std::move(prepared_standard_mapping_error);
    }
    return false;
  }

  JobsPendingPipelineStage0Submission submission{};
  submission.owner_agent_name = owner_agent_name;
  submission.lane_id = pipeline_runtime_state.next_lane_id;
  submission.loop_lane_active = false;
  submission.prepared_container_set = prepared_stage0_object.shared_container_set;
  submission.resource_bindings = resource_bindings;
  submission.descriptor_values = descriptor_values;

  JobsPipelineLaneRuntimeState queued_lane_state{};
  queued_lane_state.owner_agent_name = owner_agent_name;
  queued_lane_state.lane_id = submission.lane_id;
  queued_lane_state.loop_lane_active = false;
  queued_lane_state.standard_container_set = submission.prepared_container_set;
  queued_lane_state.resource_bindings = resource_bindings;
  queued_lane_state.descriptor_values = descriptor_values;
  queued_lane_state.stage_has_data.assign(pipeline_runtime_state.stage_has_data.size(), false);
  if (!pipeline_scheduler_detail::InitializePipelineInterStageBufferRuntimeState(
        prepared_stage0_object,
        pipeline_runtime_state.mandatory_stage_buffer_slot_name,
        &queued_lane_state.inter_stage_buffer,
        out_error_message)) {
    return false;
  }
  queued_lane_state.valid = true;

  pipeline_runtime_state.lanes.push_back(std::move(queued_lane_state));
  ++pipeline_runtime_state.next_lane_id;
  pipeline_runtime_state.pending_stage0_submissions.push_back(std::move(submission));
  pipeline_runtime_states_[pipeline_name][owner_agent_name] = std::move(pipeline_runtime_state);
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}


inline bool AlgorithmScheduler::EnqueueMountedPipelineStage0Submission(
  std::vector<::algomanager::bridge::AlgorithmObject>* mounted_objects,
  const std::string& pipeline_name,
  const std::string& owner_agent_name,
  const std::vector<AlgorithmResourceBinding>& resource_bindings,
  const std::vector<AlgorithmDescriptorValue>& descriptor_values,
  std::vector<::algomanager::bridge::AlgorithmAssemblyState>* inout_assembly_states,
  std::string* out_error_message,
  bool load_reflector) {
  auto set_error = [&](std::string message) {
    if (out_error_message) {
      *out_error_message = std::move(message);
    }
  };

  if (!mounted_objects || !inout_assembly_states) {
    set_error("Mounted pipeline stage0 submission received a null output pointer.");
    return false;
  }
  if (pipeline_name.empty()) {
    set_error("Pipeline name must not be empty.");
    return false;
  }
  for (size_t object_index = 0u; object_index < mounted_objects->size(); ++object_index) {
    ::algomanager::bridge::AlgorithmObject& object = (*mounted_objects)[object_index];
    if (object.child_algorithm_objects.empty() || object.pipeline_name != pipeline_name) {
      continue;
    }
    std::vector<::algomanager::bridge::AlgorithmAssemblyState> node_assembly_states(
      1u,
      ::algomanager::bridge::AlgorithmAssemblyState::Pending);
    if (!this->EnqueueMountedPipelineStage0SubmissionNode(
          &object,
          &node_assembly_states,
          owner_agent_name,
          resource_bindings,
          descriptor_values,
          out_error_message,
          load_reflector)) {
      return false;
    }
    if (inout_assembly_states->size() < mounted_objects->size()) {
      inout_assembly_states->resize(
        mounted_objects->size(),
        ::algomanager::bridge::AlgorithmAssemblyState::Pending);
    }
    (*inout_assembly_states)[object_index] = ::algomanager::bridge::AlgorithmAssemblyState::Ready;
    return true;
  }
  JobsPipelineRegistration registration{};
  if (!TryGetPipelineRegistration(pipeline_name, &registration)) {
    set_error("Mounted pipeline registration is unavailable.");
    return false;
  }

  std::vector<::algomanager::bridge::AlgorithmObject>& algorithm_objects = *mounted_objects;
  size_t pipeline_begin_index = algorithm_objects.size();
  size_t pipeline_end_index = algorithm_objects.size();
  if (!pipeline_scheduler_detail::FindMountedPipelineRange(
        algorithm_objects,
        pipeline_name,
        &pipeline_begin_index,
        &pipeline_end_index) ||
      pipeline_begin_index >= pipeline_end_index) {
    set_error("Mounted pipeline is unavailable.");
    return false;
  }

  const size_t body_stage0_index = pipeline_begin_index + static_cast<size_t>(registration.body_begin_stage_index);
  if (body_stage0_index >= pipeline_end_index) {
    set_error("Mounted pipeline body stage0 is unavailable.");
    return false;
  }

  ::algomanager::bridge::AlgorithmObject& stage0_object = algorithm_objects[body_stage0_index];
  if (stage0_object.execution_preference == ::algomanager::bridge::AlgorithmExecutionPreference::Jobs) {
    if (!EnqueuePipelineStage0Submission(
          pipeline_name,
          owner_agent_name,
          stage0_object.algorithm_profile.algorithm_name,
          resource_bindings,
          descriptor_values,
          out_error_message,
          load_reflector)) {
      return false;
    }
    if (inout_assembly_states->size() < algorithm_objects.size()) {
      inout_assembly_states->resize(
        algorithm_objects.size(),
        ::algomanager::bridge::AlgorithmAssemblyState::Pending);
    }
    for (size_t stage_index = pipeline_begin_index; stage_index < pipeline_end_index; ++stage_index) {
      (*inout_assembly_states)[stage_index] = ::algomanager::bridge::AlgorithmAssemblyState::Ready;
    }
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  JobsPipelineRuntimeState pipeline_state{};
  if (!TryGetPipelineRuntime(pipeline_name, owner_agent_name, &pipeline_state)) {
    set_error("Mounted pipeline is unavailable.");
    return false;
  }

  const size_t pipeline_stage_count = pipeline_end_index - pipeline_begin_index;
  pipeline_scheduler_detail::SyncPipelineLegacyStageStateFromPrimaryLane(&pipeline_state, pipeline_stage_count);
  if (pipeline_state.stage_has_data.size() != pipeline_stage_count) {
    pipeline_state.stage_has_data.assign(pipeline_stage_count, false);
  }

  const size_t valid_lane_count = pipeline_scheduler_detail::CountValidPipelineLanes(pipeline_state);
  if (valid_lane_count + pipeline_state.pending_stage0_submissions.size() >=
      static_cast<size_t>(pipeline_state.max_concurrent_stage0_submissions)) {
    pipeline_state.stage0_saturated = true;
    UpdatePipelineRuntime(pipeline_name, owner_agent_name, pipeline_state, nullptr);
    set_error("Pipeline stage0 is saturated and cannot accept more resource batches.");
    return false;
  }
  pipeline_state.stage0_saturated = false;

  ::algomanager::algoscheduler::AlgorithmObject prepared_stage0_object{};
  std::string prepare_error_message;
  if (!PrepareAlgorithmObjectByName(
        stage0_object.algorithm_profile.algorithm_name,
        resource_bindings,
        descriptor_values,
        &prepared_stage0_object,
        &prepare_error_message,
        load_reflector)) {
    set_error(prepare_error_message.empty()
      ? "Failed to prepare the pipeline stage0 submission."
      : std::move(prepare_error_message));
    return false;
  }

  JobsPendingPipelineStage0Submission submission{};
  submission.owner_agent_name = owner_agent_name;
  submission.lane_id = pipeline_state.next_lane_id;
  submission.loop_lane_active = false;
  submission.prepared_container_set = prepared_stage0_object.shared_container_set;
  submission.resource_bindings = resource_bindings;
  submission.descriptor_values = descriptor_values;

  JobsPipelineLaneRuntimeState queued_lane_state{};
  queued_lane_state.owner_agent_name = owner_agent_name;
  queued_lane_state.lane_id = submission.lane_id;
  queued_lane_state.loop_lane_active = false;
  queued_lane_state.standard_container_set = submission.prepared_container_set;
  queued_lane_state.resource_bindings = resource_bindings;
  queued_lane_state.descriptor_values = descriptor_values;
  queued_lane_state.stage_has_data.assign(pipeline_stage_count, false);
  if (!pipeline_scheduler_detail::InitializePipelineInterStageBufferRuntimeState(
        prepared_stage0_object,
        pipeline_state.mandatory_stage_buffer_slot_name,
        &queued_lane_state.inter_stage_buffer,
        out_error_message)) {
    return false;
  }
  queued_lane_state.valid = true;

  pipeline_state.lanes.push_back(std::move(queued_lane_state));
  ++pipeline_state.next_lane_id;
  pipeline_state.pending_stage0_submissions.push_back(std::move(submission));
  UpdatePipelineRuntime(pipeline_name, owner_agent_name, pipeline_state, nullptr);

  if (inout_assembly_states->size() < algorithm_objects.size()) {
    inout_assembly_states->resize(
      algorithm_objects.size(),
      ::algomanager::bridge::AlgorithmAssemblyState::Pending);
  }
  for (size_t stage_index = pipeline_begin_index; stage_index < pipeline_end_index; ++stage_index) {
    (*inout_assembly_states)[stage_index] = ::algomanager::bridge::AlgorithmAssemblyState::Ready;
  }

  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

inline bool AlgorithmScheduler::EnqueueMountedPipelineStage0SubmissionNode(
  ::algomanager::bridge::AlgorithmObject* pipeline_node,
  std::vector<::algomanager::bridge::AlgorithmAssemblyState>* inout_assembly_states,
  const std::string& owner_agent_name,
  const std::vector<AlgorithmResourceBinding>& resource_bindings,
  const std::vector<AlgorithmDescriptorValue>& descriptor_values,
  std::string* out_error_message,
  bool load_reflector) {
  if (!pipeline_node || pipeline_node->child_algorithm_objects.empty()) {
    if (out_error_message) {
      *out_error_message = "Pipeline submission node is unavailable.";
    }
    return false;
  }

  std::vector<::algomanager::bridge::AlgorithmObject>& child_objects =
    pipeline_node->child_algorithm_object_storage->children;
  std::vector<::algomanager::bridge::AlgorithmAssemblyState> child_assembly_states(
    child_objects.size(),
    ::algomanager::bridge::AlgorithmAssemblyState::Ready);
  if (!this->EnqueueMountedPipelineStage0Submission(
        &child_objects,
        pipeline_node->pipeline_name,
        owner_agent_name,
        resource_bindings,
        descriptor_values,
        &child_assembly_states,
        out_error_message,
        load_reflector)) {
    return false;
  }
  if (inout_assembly_states->empty()) {
    inout_assembly_states->push_back(::algomanager::bridge::AlgorithmAssemblyState::Ready);
  } else {
    (*inout_assembly_states)[0] = ::algomanager::bridge::AlgorithmAssemblyState::Ready;
  }
  return true;
}

inline bool AlgorithmScheduler::TickMountedPipelineNode(
  ::algomanager::bridge::AlgorithmObject* pipeline_node,
  ::algomanager::bridge::AgentAlgorithmRuntimeState* inout_runtime_state,
  const std::string& owner_agent_name,
  const ::algomanager::algoscheduler::AgentTickContext& context,
  bool allow_tick,
  const ::algomanager::bridge::AlgorithmAssemblyState& assembly_state,
  bool collect_pipeline_timing,
  common_data::AlgorithmToAgentSignal* out_pipeline_signal,
  bool* out_pipeline_processing_failed,
  std::string* out_error_message) {
  if (!pipeline_node || pipeline_node->child_algorithm_objects.empty() || !inout_runtime_state) {
    if (out_error_message) {
      *out_error_message = "Pipeline node runtime is unavailable.";
    }
    return false;
  }

  std::vector<::algomanager::bridge::AlgorithmObject>& child_objects =
    pipeline_node->child_algorithm_object_storage->children;
  std::vector<::algomanager::bridge::AgentAlgorithmRuntimeState> child_runtime_states =
    inout_runtime_state->child_runtime_states;
  child_runtime_states.resize(child_objects.size());
  for (size_t index = 0u; index < child_objects.size(); ++index) {
    child_runtime_states[index].algorithm_name = child_objects[index].algorithm_profile.algorithm_name;
  }
  std::vector<::algomanager::bridge::AlgorithmAssemblyState> child_assembly_states(
    child_objects.size(),
    assembly_state);
  std::vector<bool> child_allow_tick(child_objects.size(), allow_tick);
  child_objects.front().pipeline_stage_debug_all = pipeline_node->pipeline_stage_debug_all;
  child_objects.front().pipeline_stage_debug_index = pipeline_node->pipeline_stage_debug_index;

  if (!this->TickMountedPipeline(
        &child_objects,
        0u,
        child_objects.size(),
        owner_agent_name,
        context,
        child_allow_tick,
        child_assembly_states,
        collect_pipeline_timing,
        &child_runtime_states,
        out_pipeline_signal,
        out_pipeline_processing_failed,
        out_error_message)) {
    return false;
  }
  const std::string pipeline_name = pipeline_node->pipeline_name;
  *inout_runtime_state = child_runtime_states.front();
  inout_runtime_state->algorithm_name = pipeline_name;
  inout_runtime_state->child_runtime_states = std::move(child_runtime_states);
  return true;
}

inline void RefreshAlgorithmObjectSignals(
  ::algomanager::bridge::AlgorithmObject& object,
  ::algomanager::bridge::AgentAlgorithmRuntimeState& runtime_state,
  const ::algomanager::algoscheduler::AgentTickContext& context) {
  runtime_state.agent_to_algorithm_signal = {};
  if (object.intervention) {
    object.intervention->FillAgentToAlgorithmSignal(
      context,
      &runtime_state.agent_to_algorithm_signal);
  }
  runtime_state.child_runtime_states.resize(object.child_algorithm_objects.size());
  for (size_t child_index = 0u; child_index < object.child_algorithm_objects.size(); ++child_index) {
    RefreshAlgorithmObjectSignals(
      *object.child_algorithm_objects[child_index],
      runtime_state.child_runtime_states[child_index],
      context);
  }
}

inline bool TickAlgorithmObject(
  ::algomanager::bridge::AlgorithmObject& object,
  ::algomanager::bridge::AgentAlgorithmRuntimeState& runtime_state,
  const std::string& owner_agent_name,
  const ::algomanager::algoscheduler::AgentTickContext& context,
  bool allow_tick,
  const ::algomanager::bridge::AlgorithmAssemblyState& assembly_state,
  bool collect_pipeline_timing,
  std::string* out_error_message) {
  if (!object.child_algorithm_objects.empty()) {
    const auto pipeline_tick_begin = std::chrono::steady_clock::now();
    common_data::AlgorithmToAgentSignal pipeline_signal{};
    bool pipeline_processing_failed = false;
    if (!AlgorithmScheduler::Instance().TickMountedPipelineNode(
          &object,
          &runtime_state,
          owner_agent_name,
          context,
          allow_tick,
          assembly_state,
          collect_pipeline_timing,
          &pipeline_signal,
          &pipeline_processing_failed,
          out_error_message)) {
      return false;
    }
    if (collect_pipeline_timing && runtime_state.pipeline_stage_runtime_stats.empty()) {
      runtime_state.pipeline_total_elapsed_seconds =
        std::chrono::duration<float>(std::chrono::steady_clock::now() - pipeline_tick_begin).count();
      runtime_state.pipeline_stage_runtime_stats.reserve(object.child_algorithm_objects.size());
      for (const std::shared_ptr<::algomanager::bridge::AlgorithmObject>& child : object.child_algorithm_objects) {
        runtime_state.pipeline_stage_runtime_stats.push_back(
          ::algomanager::bridge::AlgorithmPipelineStageRuntimeStat{
            .stage_name = child->algorithm_profile.algorithm_name,
            .elapsed_seconds = 0.0f,
            .reason = {},
          });
      }
    }
    pipeline_scheduler_detail::MergeAlgorithmToAgentSignal(
      pipeline_signal,
      &runtime_state.algorithm_to_agent_signal);
    return !pipeline_processing_failed;
  }

  const bool is_ready = assembly_state == ::algomanager::bridge::AlgorithmAssemblyState::Ready;
  const bool execute_now = allow_tick && is_ready;
  return pipeline_scheduler_detail::TickAlgorithmObject(
    object,
    context,
    allow_tick,
    is_ready,
    execute_now,
    &runtime_state);
}

inline bool AlgorithmScheduler::ReplayMountedPipelineDebugNode(
  ::algomanager::bridge::AlgorithmObject* pipeline_node,
  ::algomanager::bridge::AgentAlgorithmRuntimeState* inout_runtime_state,
  size_t child_index,
  const ::algomanager::algoscheduler::AgentTickContext& context,
  std::string* out_error_message) {
  if (!pipeline_node || pipeline_node->child_algorithm_objects.empty() || !inout_runtime_state) {
    if (out_error_message) {
      *out_error_message = "Pipeline node replay runtime is unavailable.";
    }
    return false;
  }
  std::vector<::algomanager::bridge::AlgorithmObject>& child_objects =
    pipeline_node->child_algorithm_object_storage->children;
  std::vector<::algomanager::bridge::AgentAlgorithmRuntimeState> child_runtime_states =
    inout_runtime_state->child_runtime_states;
  child_runtime_states.resize(child_objects.size());
  for (size_t index = 0u; index < child_objects.size(); ++index) {
    child_runtime_states[index].algorithm_name = child_objects[index].algorithm_profile.algorithm_name;
  }
  if (!this->ReplayMountedPipelineDebug(
        &child_objects,
        child_index,
        context,
        &child_runtime_states,
        out_error_message)) {
    return false;
  }
  inout_runtime_state->child_runtime_states = child_runtime_states;
  inout_runtime_state->bridge_debug_set = child_runtime_states[child_index].bridge_debug_set;
  return true;
}


}  // namespace scheduler
}  // namespace algomanager
