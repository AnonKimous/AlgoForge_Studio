#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

#define ALGORITHM_MANAGEMENT_LAYER_PUBLIC_FACADE_INCLUDE 1
#include "algorithm_support/algorithm_container_manifest.h"
#include "algorithm_support/algorithm_intervention.h"
#include "algorithm_support/algorithm_package_location.h"
#include "algorithm_support/algorithm_protocol.h"
#include "algorithm_support/algorithm_types.h"
#undef ALGORITHM_MANAGEMENT_LAYER_PUBLIC_FACADE_INCLUDE

#define RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE 1
#include "runtime_systems/runtime_systems.h"
#undef RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE

namespace agent_management {
struct AlgorithmPipelineStallReport;
bool ReportAlgorithmPipelineStall(
  const AlgorithmPipelineStallReport& report,
  std::string* out_error_message);
}  // namespace agent_management

namespace algorithm_management {

using ::algorithm::AlgorithmContainer;
using ::algorithm::AlgorithmContainerManifest;
using ::algorithm::AlgorithmContainerManifestItem;
using ::algorithm::AlgorithmContainerSet;
using ::algorithm::AlgorithmContainerStorageKind;
using ::algorithm::AlgorithmPackageLocation;
using ::algorithm::AlgorithmProfile;
using ::algorithm::AlgorithmReflectionBinding;
using ::algorithm::AlgorithmReflector;
using ::algorithm::AlgorithmReflectorManifestItem;
using ::algorithm_management::AlgorithmPipelineStageSubmission;
using ::algorithm::AlgorithmStandardContainerLayout;
using ::algorithm::FindAlgorithmContainer;
using ::algorithm::TryResolveAlgorithmPackageLocation;

class AlgorithmScheduler {
 public:
  static AlgorithmScheduler& Instance();

  bool SubmitAlgorithmObject(
    const ::agent::AlgorithmObject& object,
    const ::agent::AgentTickContext& context,
    const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
    ::algorithm::AlgorithmContainerSet* container_set,
    common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
    ::agent::AlgorithmPackageDebugState* out_debug_state,
    std::string* out_error_message = nullptr);

  bool RegisterPipeline(
    const runtime_systems::CpuPipelineRegistration& registration,
    std::string* out_error_message = nullptr);

  bool RegisterPipelineRuntime(
    const std::string& pipeline_name,
    const std::string& owner_agent_name,
    const runtime_systems::CpuPipelineRuntimeState& runtime_state,
    std::string* out_error_message = nullptr);

  bool EnqueuePipelineStage0Submission(
    const std::string& pipeline_name,
    const std::string& owner_agent_name,
    const std::string& stage0_algorithm_name,
    const std::vector<AlgorithmResourceBinding>& resource_bindings,
    const std::vector<AlgorithmDescriptorValue>& descriptor_values,
    std::string* out_error_message = nullptr);

  bool TickMountedPipeline(
    std::vector<::algorithm_management::AlgorithmObject>* mounted_objects,
    size_t begin_index,
    size_t end_index,
    const std::string& owner_agent_name,
    const ::agent::AgentTickContext& context,
    const std::vector<bool>& allow_tick_mask,
    const std::vector<::algorithm_management::AlgorithmAssemblyState>& assembly_states,
    std::vector<::algorithm_management::AgentAlgorithmRuntimeState>* inout_runtime_states,
    common_data::AlgorithmToAgentSignal* out_pipeline_signal,
    bool* out_pipeline_processing_failed,
    std::string* out_error_message = nullptr);

  void UnregisterPipeline(const std::string& pipeline_name, const std::string& owner_agent_name);

  bool TryGetPipelineRegistration(
    const std::string& pipeline_name,
    runtime_systems::CpuPipelineRegistration* out_registration) const;

  bool TryGetPipelineRuntime(
    const std::string& pipeline_name,
    const std::string& owner_agent_name,
    runtime_systems::CpuPipelineRuntimeState* out_runtime_state) const;

  bool UpdatePipelineRuntime(
    const std::string& pipeline_name,
    const std::string& owner_agent_name,
    const runtime_systems::CpuPipelineRuntimeState& runtime_state,
    std::string* out_error_message = nullptr);

  void Clear();

 private:
  AlgorithmScheduler() = default;

  mutable std::mutex mutex_{};
  // Shared pipeline runtime storage keyed by pipeline name.
  std::unordered_map<std::string, std::unordered_map<std::string, runtime_systems::CpuPipelineRuntimeState>> pipeline_runtime_states_{};
  // Reference count for shared pipeline names across agents.
  std::unordered_map<std::string, std::unordered_map<std::string, size_t>> pipeline_runtime_ref_counts_{};
};

inline bool CreateAlgorithmObjectFromLocation(
  const ::algorithm::AlgorithmPackageLocation& package_location,
  ::agent::AlgorithmObject* out_group,
  std::string* out_error_message = nullptr) {
  return algorithm_support::CreateAlgorithmObjectFromLocation(
    package_location,
    out_group,
    out_error_message);
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

  return algorithm_support::QueryAlgorithmPackageRequestedBindingsFromLocation(
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

  return algorithm_support::LoadAlgorithmPackageDefaultBindingsFromLocation(
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
  return algorithm_support::LoadAlgorithmPackageDefaultBindingsFromLocation(
    package_location,
    out_resource_bindings,
    out_descriptor_values,
    out_has_default_file,
    out_error_message);
}

inline bool FinalizeAlgorithmObject(
  const ::agent::AlgorithmObject& algorithm_object,
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

  const bool ok = algorithm_support::DecomposeAlgorithmPackageFromLocation(
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
  ::agent::AlgorithmObject* out_object,
  std::string* out_error_message = nullptr) {
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

  ::agent::AlgorithmObject prepared_object{};
  std::string create_error_message;
  if (!CreateAlgorithmObjectFromLocation(package_location, &prepared_object, &create_error_message)) {
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

inline bool SubmitAlgorithmObject(
  const ::agent::AlgorithmObject& object,
  const ::agent::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  ::algorithm::AlgorithmContainerSet* container_set,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  ::agent::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message = nullptr) {
  return AlgorithmScheduler::Instance().SubmitAlgorithmObject(
    object,
    context,
    agent_to_algorithm_signal,
    container_set,
    out_algorithm_to_agent_signal,
    out_debug_state,
    out_error_message);
}

inline IoBufferPacket BuildAlgorithmInterventionPacket(
  const AlgorithmInterventionDescriptor& descriptor) {
  return algorithm_support::BuildAlgorithmInterventionPacket(descriptor);
}

inline bool DecodeAlgorithmInterventionPacket(
  const IoBufferPacket& packet,
  DecodedAlgorithmIntervention* decoded) {
  return algorithm_support::DecodeAlgorithmInterventionPacket(packet, decoded);
}

inline IoBufferPacket BuildAlgorithmInterventionPacket(
  const InteractionInterventionRequest& request) {
  return algorithm_support::BuildAlgorithmInterventionPacket(request);
}

inline bool DecodeAlgorithmInterventionPacket(
  const IoBufferPacket& packet,
  InteractionInterventionRequest* request) {
  return algorithm_support::DecodeAlgorithmInterventionPacket(packet, request);
}

inline AlgorithmScheduler& AlgorithmScheduler::Instance() {
  static AlgorithmScheduler instance{};
  return instance;
}

inline void AlgorithmScheduler::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  pipeline_runtime_states_.clear();
  pipeline_runtime_ref_counts_.clear();
  runtime_systems::job_cpu::Clear();
  runtime_systems::job_gpu::Clear();
}

inline bool AlgorithmScheduler::SubmitAlgorithmObject(
  const ::agent::AlgorithmObject& object,
  const ::agent::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  ::algorithm::AlgorithmContainerSet* container_set,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  ::agent::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message) {
  if (!container_set || !out_algorithm_to_agent_signal || !out_debug_state) {
    if (out_error_message) {
      *out_error_message = "Algorithm submit output pointer is null.";
    }
    return false;
  }

  if (object.execution_preference == AlgorithmExecutionPreference::Gpu) {
    if (!object.gpu_symbol) {
      if (out_error_message) {
        *out_error_message = "Ready GPU algorithm is missing a GPU symbol.";
      }
      return false;
    }

    if (!runtime_systems::job_gpu::Execute(
          object,
          container_set,
          context,
          out_error_message)) {
      return false;
    }

    if (object.algorithm_reflector && !object.algorithm_reflector->empty()) {
      if (!runtime_systems::job_gpu::Synchronize(
            object,
            container_set,
            out_error_message)) {
        return false;
      }
    }
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  if (!object.cpu_symbol || !object.temporaryTest_main_thread_executor) {
    if (out_error_message) {
      *out_error_message = "Ready CPU algorithm is missing its main-thread executor.";
    }
    return false;
  }

  const bool ok = runtime_systems::job_cpu::Execute(
    object,
    context,
    agent_to_algorithm_signal,
    container_set,
    out_algorithm_to_agent_signal,
    out_debug_state,
    out_error_message);
  if (ok && out_error_message) {
    out_error_message->clear();
  }
  return ok;
}

inline bool AlgorithmScheduler::RegisterPipeline(
  const runtime_systems::CpuPipelineRegistration& registration,
  std::string* out_error_message) {
  const bool ok = runtime_systems::job_cpu::RegisterPipeline(
    registration,
    out_error_message);
  if (ok) {
    std::lock_guard<std::mutex> lock(mutex_);
    pipeline_runtime_states_.erase(registration.pipeline_name);
  }
  return ok;
}

inline bool AlgorithmScheduler::RegisterPipelineRuntime(
  const std::string& pipeline_name,
  const std::string& owner_agent_name,
  const runtime_systems::CpuPipelineRuntimeState& runtime_state,
  std::string* out_error_message) {
  std::lock_guard<std::mutex> lock(mutex_);
  pipeline_runtime_states_[pipeline_name][owner_agent_name] = runtime_state;
  ++pipeline_runtime_ref_counts_[pipeline_name][owner_agent_name];
  if (out_error_message) {
    out_error_message->clear();
  }

  runtime_systems::CpuPipelineRegistration registration{};
  if (runtime_systems::job_cpu::TryGetPipelineRegistration(
        pipeline_name,
        &registration)) {
    return runtime_systems::job_cpu::RegisterPipelineRuntime(
      pipeline_name,
      owner_agent_name,
      runtime_state,
      out_error_message);
  }
  return true;
}

inline bool AlgorithmScheduler::EnqueuePipelineStage0Submission(
  const std::string& pipeline_name,
  const std::string& owner_agent_name,
  const std::string& stage0_algorithm_name,
  const std::vector<AlgorithmResourceBinding>& resource_bindings,
  const std::vector<AlgorithmDescriptorValue>& descriptor_values,
  std::string* out_error_message) {
  runtime_systems::CpuPipelineRegistration registration{};
  if (runtime_systems::job_cpu::TryGetPipelineRegistration(
        pipeline_name,
        &registration)) {
    runtime_systems::CpuPipelineRuntimeState pipeline_runtime_state{};
    {
      std::lock_guard<std::mutex> lock(mutex_);
      const auto found = pipeline_runtime_states_.find(pipeline_name);
      if (found == pipeline_runtime_states_.end()) {
        if (out_error_message) {
          *out_error_message = "CPU pipeline runtime state is unavailable.";
        }
        return false;
      }
      const auto owner_found = found->second.find(owner_agent_name);
      if (owner_found == found->second.end()) {
        if (out_error_message) {
          *out_error_message = "CPU pipeline runtime state is unavailable for the selected agent.";
        }
        return false;
      }
      pipeline_runtime_state = owner_found->second;
    }

    pipeline_runtime_state.max_concurrent_stage0_submissions =
      registration.max_concurrent_stage0_submissions;
    if (!runtime_systems::job_cpu::UpdatePipelineRuntime(
          pipeline_name,
          owner_agent_name,
          pipeline_runtime_state,
          out_error_message)) {
      return false;
    }
    if (!runtime_systems::job_cpu::EnqueuePipelineStage0Submission(
          pipeline_name,
          owner_agent_name,
          stage0_algorithm_name,
          resource_bindings,
          descriptor_values,
          out_error_message)) {
      return false;
    }
    if (!runtime_systems::job_cpu::TryGetPipelineRuntime(
          pipeline_name,
          owner_agent_name,
          &pipeline_runtime_state)) {
      if (out_error_message) {
        *out_error_message = "CPU pipeline runtime state is unavailable.";
      }
      return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    pipeline_runtime_states_[pipeline_name][owner_agent_name] = pipeline_runtime_state;
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  std::lock_guard<std::mutex> lock(mutex_);
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
  runtime_systems::CpuPipelineRuntimeState pipeline_runtime_state = owner_found->second;
  if (pipeline_runtime_state.stage_has_data.size() != pipeline_runtime_state.lanes.size()) {
    pipeline_runtime_state.stage_has_data.assign(pipeline_runtime_state.lanes.size(), false);
  }
  size_t valid_lane_count = 0u;
  for (const runtime_systems::CpuPipelineLaneRuntimeState& lane_state : pipeline_runtime_state.lanes) {
    if (lane_state.valid) {
      ++valid_lane_count;
    }
  }
  if (valid_lane_count + pipeline_runtime_state.pending_stage0_submissions.size() >=
      static_cast<size_t>(pipeline_runtime_state.max_concurrent_stage0_submissions)) {
    pipeline_runtime_state.stage0_saturated = true;
    pipeline_runtime_states_[pipeline_name][owner_agent_name] = pipeline_runtime_state;
    if (out_error_message) {
      *out_error_message = "Pipeline stage0 is saturated and cannot accept more resource batches.";
    }
    return false;
  }
  pipeline_runtime_state.stage0_saturated = false;

  ::agent::AlgorithmObject prepared_stage0_object{};
  std::string prepare_error_message;
  if (!PrepareAlgorithmObjectByName(
        stage0_algorithm_name,
        resource_bindings,
        descriptor_values,
        &prepared_stage0_object,
        &prepare_error_message)) {
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

  runtime_systems::CpuPendingPipelineStage0Submission submission{};
  submission.owner_agent_name = owner_agent_name;
  submission.lane_id = pipeline_runtime_state.next_lane_id;
  submission.loop_lane_active = false;
  submission.prepared_container_set = prepared_stage0_object.shared_container_set;
  submission.resource_bindings = resource_bindings;
  submission.descriptor_values = descriptor_values;

  runtime_systems::CpuPipelineLaneRuntimeState queued_lane_state{};
  queued_lane_state.owner_agent_name = owner_agent_name;
  queued_lane_state.lane_id = submission.lane_id;
  queued_lane_state.loop_lane_active = false;
  queued_lane_state.standard_container_set = submission.prepared_container_set;
  queued_lane_state.resource_bindings = resource_bindings;
  queued_lane_state.descriptor_values = descriptor_values;
  queued_lane_state.stage_has_data.assign(pipeline_runtime_state.stage_has_data.size(), false);
  queued_lane_state.inter_stage_buffer.standard_container_slot_name =
    pipeline_runtime_state.mandatory_stage_buffer_slot_name;
  queued_lane_state.inter_stage_buffer.valid = true;
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

inline bool AlgorithmScheduler::TickMountedPipeline(
  std::vector<::algorithm_management::AlgorithmObject>* mounted_objects,
  size_t begin_index,
  size_t end_index,
  const std::string& owner_agent_name,
  const ::agent::AgentTickContext& context,
  const std::vector<bool>& allow_tick_mask,
  const std::vector<::algorithm_management::AlgorithmAssemblyState>& assembly_states,
  std::vector<::algorithm_management::AgentAlgorithmRuntimeState>* inout_runtime_states,
  common_data::AlgorithmToAgentSignal* out_pipeline_signal,
  bool* out_pipeline_processing_failed,
  std::string* out_error_message) {
  if (!mounted_objects || begin_index >= end_index || end_index > mounted_objects->size()) {
    if (out_error_message) {
      *out_error_message = "CPU pipeline tick received an invalid pipeline range.";
    }
    return false;
  }

  const std::string& pipeline_name = (*mounted_objects)[begin_index].pipeline_name;
  runtime_systems::CpuPipelineRuntimeState pipeline_runtime_state{};
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = pipeline_runtime_states_.find(pipeline_name);
    if (found != pipeline_runtime_states_.end()) {
      const auto owner_found = found->second.find(owner_agent_name);
      if (owner_found != found->second.end()) {
        pipeline_runtime_state = owner_found->second;
      }
    }
  }
  if (!runtime_systems::job_cpu::UpdatePipelineRuntime(
        pipeline_name,
        owner_agent_name,
        pipeline_runtime_state,
        out_error_message)) {
    return false;
  }
  const bool ok = runtime_systems::job_cpu::TickMountedPipeline(
    mounted_objects,
    begin_index,
    end_index,
    owner_agent_name,
    context,
    allow_tick_mask,
    assembly_states,
    inout_runtime_states,
    out_pipeline_signal,
    out_pipeline_processing_failed,
    out_error_message);
  if (!ok) {
    return false;
  }
  if (!runtime_systems::job_cpu::TryGetPipelineRuntime(
        pipeline_name,
        owner_agent_name,
        &pipeline_runtime_state)) {
    if (out_error_message) {
      *out_error_message = "CPU pipeline runtime state is unavailable.";
    }
    return false;
  }
  if (inout_runtime_states && end_index > begin_index && end_index <= inout_runtime_states->size()) {
    const ::algorithm_management::AgentAlgorithmRuntimeState& exit_runtime_state =
      (*inout_runtime_states)[end_index - 1u];
    if (exit_runtime_state.reflection_snapshot.valid) {
      pipeline_runtime_state.exit_reflection_snapshot = exit_runtime_state.reflection_snapshot;
      pipeline_runtime_state.exit_reflection_snapshot_valid = true;
    }
  }
  if (!runtime_systems::job_cpu::UpdatePipelineRuntime(
        pipeline_name,
        owner_agent_name,
        pipeline_runtime_state,
        out_error_message)) {
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pipeline_runtime_states_[pipeline_name][owner_agent_name] = pipeline_runtime_state;
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

inline void AlgorithmScheduler::UnregisterPipeline(const std::string& pipeline_name, const std::string& owner_agent_name) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto ref_count_it = pipeline_runtime_ref_counts_.find(pipeline_name);
  if (ref_count_it != pipeline_runtime_ref_counts_.end()) {
    const auto owner_ref_count_it = ref_count_it->second.find(owner_agent_name);
    if (owner_ref_count_it != ref_count_it->second.end()) {
      if (owner_ref_count_it->second > 1u) {
        --owner_ref_count_it->second;
        return;
      }
      ref_count_it->second.erase(owner_ref_count_it);
    }
    if (ref_count_it->second.empty()) {
      pipeline_runtime_ref_counts_.erase(ref_count_it);
    }
  }
  const auto runtime_states_it = pipeline_runtime_states_.find(pipeline_name);
  if (runtime_states_it != pipeline_runtime_states_.end()) {
    runtime_states_it->second.erase(owner_agent_name);
    if (runtime_states_it->second.empty()) {
      pipeline_runtime_states_.erase(runtime_states_it);
    }
  }
  runtime_systems::job_cpu::UnregisterPipeline(pipeline_name, owner_agent_name);
}

inline bool AlgorithmScheduler::TryGetPipelineRegistration(
  const std::string& pipeline_name,
  runtime_systems::CpuPipelineRegistration* out_registration) const {
  return runtime_systems::job_cpu::TryGetPipelineRegistration(
    pipeline_name,
    out_registration);
}

inline bool AlgorithmScheduler::TryGetPipelineRuntime(
  const std::string& pipeline_name,
  const std::string& owner_agent_name,
  runtime_systems::CpuPipelineRuntimeState* out_runtime_state) const {
  if (!out_runtime_state || pipeline_name.empty()) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = pipeline_runtime_states_.find(pipeline_name);
  if (found == pipeline_runtime_states_.end()) {
    return false;
  }
  const auto owner_found = found->second.find(owner_agent_name);
  if (owner_found == found->second.end()) {
    return false;
  }
  *out_runtime_state = owner_found->second;
  return true;
}

inline bool AlgorithmScheduler::UpdatePipelineRuntime(
  const std::string& pipeline_name,
  const std::string& owner_agent_name,
  const runtime_systems::CpuPipelineRuntimeState& runtime_state,
  std::string* out_error_message) {
  std::lock_guard<std::mutex> lock(mutex_);
  pipeline_runtime_states_[pipeline_name][owner_agent_name] = runtime_state;
  if (out_error_message) {
    out_error_message->clear();
  }
  runtime_systems::CpuPipelineRegistration registration{};
  if (runtime_systems::job_cpu::TryGetPipelineRegistration(
        pipeline_name,
        &registration)) {
    return runtime_systems::job_cpu::UpdatePipelineRuntime(
      pipeline_name,
      owner_agent_name,
      runtime_state,
      out_error_message);
  }
  return true;
}

}  // namespace algorithm_management

using algorithm_management::AlgorithmContainer;
using algorithm_management::AlgorithmContainerSet;
using algorithm_management::AlgorithmContainerManifest;
using algorithm_management::AlgorithmContainerManifestItem;
using algorithm_management::AlgorithmContainerStorageKind;
using algorithm_management::AlgorithmStandardContainerLayout;
using algorithm_management::AlgorithmProfile;
using algorithm_management::AlgorithmReflectionBinding;
using algorithm_management::AlgorithmReflector;
using algorithm_management::AlgorithmReflectorManifestItem;
using algorithm_management::PipelineStageBridge;
using algorithm_management::AlgorithmRuntimeTransferBinding;
using algorithm_management::AlgorithmRuntimeTransferEdge;
using algorithm_management::AlgorithmRuntimeTransferMap;
using algorithm_management::AlgorithmPackageLocation;
using algorithm_management::FindAlgorithmContainer;
using algorithm_management::CreateAlgorithmObjectFromLocation;
using algorithm_management::FinalizeAlgorithmObject;
using algorithm_management::PrepareAlgorithmObjectByName;
using algorithm_management::PipelineStageBridgeIngress;
using algorithm_management::PipelineStageBridgeEgress;
using algorithm_management::PipelineStageBridgeCaptureIngressDebugSet;
using algorithm_management::PipelineStageBridgeCaptureEgressDebugSet;
using algorithm_management::LoadAlgorithmPackageTransferMapFromLocation;
using algorithm_management::QueryAlgorithmRequestedBindings;
using algorithm_management::LoadAlgorithmPackageDefaultBindings;
using algorithm_management::LoadAlgorithmPackageDefaultBindingsFromLocation;
using algorithm_management::SubmitAlgorithmObject;
using algorithm_management::TryResolveAlgorithmPackageLocation;
using agent_management::AlgorithmPipelineStallReport;
using agent_management::ReportAlgorithmPipelineStall;
