#include "debug_tool_backend_runtime.h"

#include "common_data/kernel_cfg.h"
#include "cJSON.h"

#include <algorithm>
#include <cstdlib>
#include <cassert>
#include <chrono>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_set>

// Keep the existing implementation readable while routing all backend-facing
// hooker calls through the single unified facade.
namespace hook = debug_tool_backend::hooker;
using namespace hook;
namespace agent_hooker = hook;
namespace algorithm_manager_hooker = hook;

namespace debug_tool_backend {

namespace {

#ifndef NDEBUG
#define DEBUG_TOOL_ASSERT(condition, message) do { \
  if (!(condition)) { \
    std::cerr << (message) << '\n'; \
    assert((condition) && (message)); \
  } \
} while (false)
#else
#define DEBUG_TOOL_ASSERT(condition, message) ((void)0)
#endif

std::string _HotReloadBuildCommand(const std::string& algorithm_name) {
  return hook::HotReloadBuildCommand(algorithm_name);
}

std::string _ResolveAlgorithmShaderPath(
  const agentmanager::agent::AlgorithmObject& object,
  const std::string& shader_path) {
  return hook::ResolveAlgorithmShaderPath(object, shader_path);
}

std::string _ResolveShaderBinaryPath(const std::string& shader_path) {
  return hook::ResolveShaderBinaryPath(shader_path);
}

bool _IsReadableNonEmptyFile(const std::string& path) {
  return hook::IsReadableNonEmptyFile(path);
}

const agentmanager::agent::AlgorithmReflectionValue* _FindReflectionValue(
  const agentmanager::agent::AlgorithmReflectionSnapshot& snapshot,
  const std::string& container_name) {
  return hook::FindReflectionValue(snapshot, container_name);
}

std::string _AlgorithmContainerStorageKindToString(algorithm::AlgorithmContainerStorageKind storage_kind) {
  switch (storage_kind) {
    case algorithm::AlgorithmContainerStorageKind::Array: return "array";
    case algorithm::AlgorithmContainerStorageKind::TemporaryRegister: return "temporary_register";
    case algorithm::AlgorithmContainerStorageKind::TemporaryCache: return "temporary_cache";
  }
  return "unknown";
}

debug_tool::AlgorithmAssemblyState _ToDebugToolAlgorithmAssemblyState(agentmanager::agent::AlgorithmAssemblyState state) {
  switch (state) {
    case agentmanager::agent::AlgorithmAssemblyState::Pending: return debug_tool::AlgorithmAssemblyState::Pending;
    case agentmanager::agent::AlgorithmAssemblyState::Assembling: return debug_tool::AlgorithmAssemblyState::Assembling;
    case agentmanager::agent::AlgorithmAssemblyState::Ready: return debug_tool::AlgorithmAssemblyState::Ready;
    case agentmanager::agent::AlgorithmAssemblyState::Failed: return debug_tool::AlgorithmAssemblyState::Failed;
  }
  return debug_tool::AlgorithmAssemblyState::Failed;
}

debug_tool::AlgorithmReflectionValue _ToDebugToolReflectionValue(const agentmanager::agent::AlgorithmReflectionValue& value) {
  debug_tool::AlgorithmReflectionValue out_value{};
  out_value.reflection_object_name = value.reflection_object_name;
  out_value.container_name = value.container_name;
  out_value.filter_name = value.filter_name;
  out_value.storage_kind = _AlgorithmContainerStorageKindToString(value.storage_kind);
  out_value.bytes = value.bytes;
  return out_value;
}

debug_tool::AlgorithmReflectionSnapshot _ToDebugToolReflectionSnapshot(
  const agentmanager::agent::AlgorithmReflectionSnapshot& snapshot) {
  debug_tool::AlgorithmReflectionSnapshot out_snapshot{};
  out_snapshot.algorithm_name = snapshot.algorithm_name;
  out_snapshot.valid = snapshot.valid;
  out_snapshot.variables.reserve(snapshot.variables.size());
  for (const agentmanager::agent::AlgorithmReflectionValue& value : snapshot.variables) {
    out_snapshot.variables.push_back(_ToDebugToolReflectionValue(value));
  }
  out_snapshot.variable_arrays.reserve(snapshot.variable_arrays.size());
  for (const agentmanager::agent::AlgorithmReflectionValue& value : snapshot.variable_arrays) {
    out_snapshot.variable_arrays.push_back(_ToDebugToolReflectionValue(value));
  }
  return out_snapshot;
}

void _AppendBridgeReflectionValue(
  const algorithm::AlgorithmContainer& container,
  const std::string& filter_name,
  agentmanager::agent::AlgorithmReflectionSnapshot* out_snapshot) {
  if (!out_snapshot) {
    return;
  }

  agentmanager::agent::AlgorithmReflectionValue value{};
  value.reflection_object_name = container.name;
  value.container_name = container.name;
  value.filter_name = filter_name;
  value.storage_kind = container.storage_kind;
  value.bytes.assign(container.bytes.begin(), container.bytes.end());

  if (container.storage_kind == algorithm::AlgorithmContainerStorageKind::Array) {
    out_snapshot->variable_arrays.push_back(std::move(value));
    return;
  }
  out_snapshot->variables.push_back(std::move(value));
}

agentmanager::agent::AlgorithmReflectionSnapshot _BuildBridgeReflectionSnapshot(
  const algorithm::AlgorithmContainerSet& container_set,
  const std::string& filter_name) {
  agentmanager::agent::AlgorithmReflectionSnapshot snapshot{};
  snapshot.algorithm_name = container_set.algorithm_name;
  snapshot.variables.reserve(
    container_set.temporary_registers.size() +
    container_set.temporary_caches.size() +
    container_set.hidden_containers.size());
  snapshot.variable_arrays.reserve(container_set.arrays.size());

  for (const algorithm::AlgorithmContainer& container : container_set.arrays) {
    _AppendBridgeReflectionValue(container, filter_name, &snapshot);
  }
  for (const algorithm::AlgorithmContainer& container : container_set.temporary_registers) {
    _AppendBridgeReflectionValue(container, filter_name, &snapshot);
  }
  for (const algorithm::AlgorithmContainer& container : container_set.temporary_caches) {
    _AppendBridgeReflectionValue(container, filter_name, &snapshot);
  }
  for (const algorithm::AlgorithmContainer& container : container_set.hidden_containers) {
    _AppendBridgeReflectionValue(container, filter_name, &snapshot);
  }

  snapshot.valid = !snapshot.variables.empty() || !snapshot.variable_arrays.empty();
  return snapshot;
}

debug_tool::PipelineStageBridgeDebugSummary _ToDebugToolPipelineStageBridgeDebugSummary(
  const agentmanager::agent::PipelineStageBridgeDebugSet& debug_set) {
  debug_tool::PipelineStageBridgeDebugSummary out_summary{};
  if (!debug_set.valid) {
    return out_summary;
  }

  out_summary.pipeline_name = debug_set.pipeline_name;
  out_summary.stage_name = debug_set.stage_name;
  out_summary.previous_stage_name = debug_set.previous_stage_name;
  out_summary.next_stage_name = debug_set.next_stage_name;
  out_summary.ingress_bindings.reserve(debug_set.ingress_bindings.size());
  for (const agentmanager::agent::PipelineStageBridgeDebugBinding& binding : debug_set.ingress_bindings) {
    out_summary.ingress_bindings.push_back(debug_tool::PipelineStageBridgeDebugBinding{
      .source_stage_name = binding.source_stage_name,
      .target_stage_name = binding.target_stage_name,
      .source_container_name = binding.source_container_name,
      .target_container_name = binding.target_container_name,
      .required = binding.required,
    });
  }
  out_summary.egress_bindings.reserve(debug_set.egress_bindings.size());
  for (const agentmanager::agent::PipelineStageBridgeDebugBinding& binding : debug_set.egress_bindings) {
    out_summary.egress_bindings.push_back(debug_tool::PipelineStageBridgeDebugBinding{
      .source_stage_name = binding.source_stage_name,
      .target_stage_name = binding.target_stage_name,
      .source_container_name = binding.source_container_name,
      .target_container_name = binding.target_container_name,
      .required = binding.required,
    });
  }

  if (debug_set.has_stage_input_container_set) {
    out_summary.stage_input_reflection_snapshot =
      _ToDebugToolReflectionSnapshot(
        _BuildBridgeReflectionSnapshot(debug_set.stage_input_container_set, "bridge_stage_input"));
    out_summary.logical_decomposer_snapshot = out_summary.stage_input_reflection_snapshot;
    out_summary.has_logical_decomposer_snapshot = true;
    out_summary.has_stage_input_reflection_snapshot = true;
  }
  if (debug_set.has_stage_output_container_set) {
    out_summary.stage_output_reflection_snapshot =
      _ToDebugToolReflectionSnapshot(
        _BuildBridgeReflectionSnapshot(debug_set.stage_output_container_set, "bridge_stage_output"));
    out_summary.stage_runtime_snapshot = out_summary.stage_output_reflection_snapshot;
    out_summary.has_stage_runtime_snapshot = true;
    out_summary.has_stage_output_reflection_snapshot = true;
  }
  if (debug_set.has_next_stage_input_container_set) {
    out_summary.next_stage_input_reflection_snapshot =
      _ToDebugToolReflectionSnapshot(
        _BuildBridgeReflectionSnapshot(debug_set.next_stage_input_container_set, "bridge_next_stage_input"));
    out_summary.logical_reflector_snapshot = out_summary.next_stage_input_reflection_snapshot;
    out_summary.has_logical_reflector_snapshot = true;
    out_summary.has_next_stage_input_reflection_snapshot = true;
  }
  if (debug_set.has_replay_output_container_set) {
    out_summary.replay_output_reflection_snapshot =
      _ToDebugToolReflectionSnapshot(
        _BuildBridgeReflectionSnapshot(debug_set.replay_output_container_set, "bridge_replay_output"));
    out_summary.logical_replay_reflector_snapshot = out_summary.replay_output_reflection_snapshot;
    out_summary.has_logical_replay_reflector_snapshot = true;
    out_summary.has_replay_output_reflection_snapshot = true;
  }
  if (debug_set.replay_reflection_snapshot.valid) {
    out_summary.replay_reflection_snapshot =
      _ToDebugToolReflectionSnapshot(debug_set.replay_reflection_snapshot);
  }
  out_summary.replay_valid = debug_set.replay_valid;
  out_summary.valid = true;
  return out_summary;
}

bool _TryGetMountedPipelineRegistration(
  const agentmanager::agent::AlgorithmObject& object,
  algorithmManager::JobsPipelineRegistration* out_registration) {
  return hook::TryGetMountedPipelineRegistration(object.pipeline_name, out_registration);
}

bool _TryFindPipelineGroupRange(
  const agentmanager::agent::Agent& managed_agent,
  size_t anchor_index,
  size_t* out_begin_index,
  size_t* out_end_index) {
  return hook::TryFindPipelineGroupRange(managed_agent, anchor_index, out_begin_index, out_end_index);
}

bool _TryLoadInterventionPhaseSpecs(
  const agentmanager::agent::AlgorithmObject& object,
  std::vector<agentmanager::agent::AlgorithmPhaseSpec>* out_phase_specs) {
  return hook::TryLoadInterventionPhaseSpecs(object, out_phase_specs);
}

bool _ContainsResultRenderPhase(const std::vector<agentmanager::agent::AlgorithmPhaseSpec>& phase_specs) {
  return hook::ContainsResultRenderPhase(phase_specs);
}

void _AppendUniquePipelineStageIndex(
  size_t candidate_index,
  std::unordered_set<size_t>* seen_indices,
  std::vector<size_t>* ordered_indices) {
  hook::AppendUniquePipelineStageIndex(candidate_index, seen_indices, ordered_indices);
}

void _AppendPipelineSummaryInterventionPhases(
  const agentmanager::agent::Agent& managed_agent,
  size_t stage_index,
  const algorithmManager::JobsPipelineRegistration& registration,
  std::vector<agentmanager::agent::AlgorithmPhaseSpec>* out_phase_specs) {
  hook::AppendPipelineSummaryInterventionPhases(managed_agent, stage_index, registration, out_phase_specs);
}

bool _TryResolveRenderPreviewSource(
  const agentmanager::agent::Agent& managed_agent,
  size_t selected_index,
  size_t* out_source_index,
  std::vector<agentmanager::agent::AlgorithmPhaseSpec>* out_phase_specs,
  std::string* out_error_message) {
  return hook::TryResolveRenderPreviewSource(
    managed_agent,
    selected_index,
    out_source_index,
    out_phase_specs,
    out_error_message);
}

debug_tool::AlgorithmRuntimeSummary _ToDebugToolAlgorithmRuntimeSummary(
  const agentmanager::agent::Agent& managed_agent,
  size_t algorithm_index) {
  debug_tool::AlgorithmRuntimeSummary summary{};
  const agentmanager::agent::AlgorithmObject* object = agent_hooker::AlgorithmObjectAt(managed_agent, algorithm_index);
  const agentmanager::agent::AgentAlgorithmRuntimeState* runtime_state =
    agent_hooker::AlgorithmRuntimeStateAt(managed_agent, algorithm_index);
  if (!object) {
    return summary;
  }

  summary.algorithm_name = object->algorithm_profile.algorithm_name;
  summary.runtime_package_root_path = object->runtime_package_root_path;
  if (summary.runtime_package_root_path.empty()) {
    ::algorithm::AlgorithmPackageLocation package_location{};
    std::string location_error_message;
    if (algorithm_manager_hooker::TryResolveAlgorithmPackageLocation(
          summary.algorithm_name,
          &package_location,
          &location_error_message)) {
      summary.runtime_package_root_path = package_location.runtime_package_root.generic_string();
    }
  }
  summary.assembly_state =
    _ToDebugToolAlgorithmAssemblyState(managed_agent.algorithm_assembly_state(algorithm_index));
  summary.pipeline_name = object->pipeline_name;
  summary.pipeline_root_stage_name = object->algorithm_profile.algorithm_name;
  summary.pipeline_stage_index = object->pipeline_stage_index;
  summary.pipeline_stage_count = object->pipeline_stage_count;
  summary.pipeline_stage = object->pipeline_stage;
  summary.pipeline_wrapper_role = object->pipeline_wrapper_role;
  summary.pipeline_wrapper_empty = object->pipeline_wrapper_empty;
  summary.pipeline_topology =
    static_cast<debug_tool::AlgorithmPipelineTopology>(
      static_cast<int>(object->pipeline_topology));
  summary.pipeline_sync_mode =
    static_cast<debug_tool::AlgorithmPipelineSyncMode>(
      static_cast<int>(object->pipeline_sync_mode));
  summary.resource_bindings.reserve(object->resource_bindings.size());
  for (const agentmanager::agent::AlgorithmResourceBinding& binding : object->resource_bindings) {
    summary.resource_bindings.push_back(debug_tool::AlgorithmResourceBinding{
      .resource_name = binding.resource_name,
      .resource_kind = binding.resource_kind,
      .source_path = binding.source_path,
      .required = true,
    });
  }
  summary.descriptor_values.reserve(object->descriptor_values.size());
  for (const agentmanager::agent::AlgorithmDescriptorValue& value : object->descriptor_values) {
    summary.descriptor_values.push_back(debug_tool::AlgorithmDescriptorValue{
      .descriptor_name = value.descriptor_name,
      .scalar_value = value.scalar_value,
    });
  }
  summary.jobs_symbol = object->jobs_symbol;
  summary.vk_symbol = object->vk_symbol;
  summary.compatibility_symbol = object->compatibility_symbol;
  summary.has_reflector = object->algorithm_reflector != nullptr;
  summary.has_intervention = object->intervention != nullptr;
  summary.mount_mode = static_cast<debug_tool::AlgorithmMountMode>(static_cast<int>(object->mount_mode));
  summary.execution_preference =
    static_cast<debug_tool::AlgorithmExecutionPreference>(static_cast<int>(object->execution_preference));
  algorithmManager::JobsPipelineRegistration registration{};
  const bool has_registration = _TryGetMountedPipelineRegistration(*object, &registration);
  if (has_registration) {
    summary.pipeline_root_stage_name = registration.root_stage_name.empty()
      ? object->algorithm_profile.algorithm_name
      : registration.root_stage_name;
    summary.pipeline_body_begin_stage_index = registration.body_begin_stage_index;
    summary.pipeline_body_stage_count = registration.body_stage_count;
    summary.pipeline_effective_tail_stage_index = registration.effective_tail_stage_index;
  }
  if (runtime_state) {
    summary.agent_to_algorithm_signal = runtime_state->agent_to_algorithm_signal;
    summary.algorithm_to_agent_signal = runtime_state->algorithm_to_agent_signal;
    summary.pipeline_active_stage_index = runtime_state->pipeline_active_stage_index;
    summary.pipeline_active_stage_index_valid = runtime_state->pipeline_active_stage_index_valid;
    summary.pipeline_active_bundle_begin_stage_index = runtime_state->pipeline_active_bundle_begin_stage_index;
    summary.pipeline_active_bundle_stage_count = runtime_state->pipeline_active_bundle_stage_count;
    summary.pipeline_active_bundle_preference = static_cast<debug_tool::AlgorithmExecutionPreference>(
      static_cast<int>(runtime_state->pipeline_active_bundle_preference));
    summary.pipeline_active_bundle_valid = runtime_state->pipeline_active_bundle_valid;
    summary.pipeline_total_elapsed_seconds = runtime_state->pipeline_total_elapsed_seconds;
    summary.pipeline_stage_runtime_stats = runtime_state->pipeline_stage_runtime_stats;
  }
  const agentmanager::agent::AlgorithmReflectionSnapshot* reflection_snapshot = nullptr;
  algorithmManager::JobsPipelineRuntimeState pipeline_runtime_state{};
  if (object->pipeline_stage &&
      object->pipeline_stage_index == 0u &&
      !object->pipeline_name.empty() &&
      algorithm_manager_hooker::TryGetMountedPipelineRuntime(
        object->pipeline_name,
        agent_hooker::AgentName(managed_agent),
        &pipeline_runtime_state) &&
      pipeline_runtime_state.exit_reflection_snapshot_valid) {
    reflection_snapshot = &pipeline_runtime_state.exit_reflection_snapshot;
  } else if (runtime_state && runtime_state->reflection_snapshot.valid) {
    reflection_snapshot = &runtime_state->reflection_snapshot;
  }
  if (reflection_snapshot) {
    summary.reflection_snapshot = _ToDebugToolReflectionSnapshot(*reflection_snapshot);
  }
  std::vector<algorithmManager::AlgorithmPhaseSpec> phase_specs;
  const bool primary_pipeline_stage =
    object->pipeline_stage &&
    !object->pipeline_name.empty() &&
    object->pipeline_stage_index == 0u;
  if (primary_pipeline_stage && has_registration) {
    _AppendPipelineSummaryInterventionPhases(
      managed_agent,
      algorithm_index,
      registration,
      &phase_specs);
  } else {
    _TryLoadInterventionPhaseSpecs(*object, &phase_specs);
  }
  summary.has_intervention = !phase_specs.empty();
  if (!phase_specs.empty()) {
    summary.intervention_phase_summaries.reserve(phase_specs.size());
    for (const algorithmManager::AlgorithmPhaseSpec& phase_spec : phase_specs) {
      debug_tool::AlgorithmPhaseSummary phase_summary{};
      phase_summary.phase_name = phase_spec.stage_name;
      phase_summary.phase_kind = phase_spec.stage_kind;
      phase_summary.execution_preference = phase_spec.execution_preference;
      phase_summary.functions = phase_spec.functions;
      phase_summary.used_algorithm_containers = phase_spec.used_algorithm_containers;
      phase_summary.vertex_shader_path = phase_spec.shader.vertex_shader_path;
      phase_summary.fragment_shader_path = phase_spec.shader.fragment_shader_path;
      phase_summary.pipeline_kind = phase_spec.shader.pipeline_kind;
      summary.intervention_phase_summaries.push_back(std::move(phase_summary));
    }
  }
  if (runtime_state && runtime_state->bridge_debug_set.valid) {
    summary.bridge_debug_set = _ToDebugToolPipelineStageBridgeDebugSummary(runtime_state->bridge_debug_set);
  }
  return summary;
}

std::vector<agentmanager::agent::AlgorithmResourceBinding> _ToAgentResourceBindings(
  const std::vector<debug_tool::AlgorithmResourceBinding>& bindings) {
  std::vector<agentmanager::agent::AlgorithmResourceBinding> result;
  result.reserve(bindings.size());
  for (const debug_tool::AlgorithmResourceBinding& binding : bindings) {
    result.push_back(agentmanager::agent::AlgorithmResourceBinding{
      .resource_name = binding.resource_name,
      .resource_kind = binding.resource_kind,
      .source_path = binding.source_path,
    });
  }
  return result;
}

std::vector<agentmanager::agent::AlgorithmDescriptorValue> _ToAgentDescriptorValues(
  const std::vector<debug_tool::AlgorithmDescriptorValue>& values) {
  std::vector<agentmanager::agent::AlgorithmDescriptorValue> result;
  result.reserve(values.size());
  for (const debug_tool::AlgorithmDescriptorValue& value : values) {
    result.push_back(agentmanager::agent::AlgorithmDescriptorValue{
      .descriptor_name = value.descriptor_name,
      .scalar_value = value.scalar_value,
    });
  }
  return result;
}

std::vector<agentmanager::agent::AlgorithmPipelineStageSubmission> _ToAgentPipelineStageSubmissions(
  const std::vector<debug_tool::AlgorithmPipelineStageSubmission>& stage_submissions) {
  std::vector<agentmanager::agent::AlgorithmPipelineStageSubmission> result;
  result.reserve(stage_submissions.size());
  for (const debug_tool::AlgorithmPipelineStageSubmission& stage_submission : stage_submissions) {
    result.push_back(agentmanager::agent::AlgorithmPipelineStageSubmission{
      .stage_name = stage_submission.stage_name,
      .resource_bindings = _ToAgentResourceBindings(stage_submission.resource_bindings),
      .descriptor_values = _ToAgentDescriptorValues(stage_submission.descriptor_values),
      .execution_preference = static_cast<agentmanager::agent::AlgorithmExecutionPreference>(
        static_cast<int>(stage_submission.execution_preference)),
    });
  }
  return result;
}

std::vector<debug_tool::AlgorithmPipelineStageSubmission> _MakeSingleStagePipelineSubmissions(
  const std::string& pipeline_name,
  const std::vector<debug_tool::AlgorithmResourceBinding>& resource_bindings,
  const std::vector<debug_tool::AlgorithmDescriptorValue>& descriptor_values,
  debug_tool::AlgorithmExecutionPreference execution_preference) {
  std::vector<debug_tool::AlgorithmPipelineStageSubmission> stage_submissions;
  stage_submissions.push_back(debug_tool::AlgorithmPipelineStageSubmission{
    .stage_name = pipeline_name,
    .resource_bindings = resource_bindings,
    .descriptor_values = descriptor_values,
    .execution_preference = execution_preference,
  });
  return stage_submissions;
}

bool _IsPipelineResourceBatchSubmissionName(const std::string& pipeline_name) {
  return pipeline_name.find("::testsubmit") != std::string::npos;
}

bool _FindMountedPipelineInstanceName(
  const std::shared_ptr<agentmanager::agent::Agent>& managed_agent,
  const std::string& pipeline_algorithm_name,
  std::string* out_pipeline_name,
  size_t* out_pipeline_index,
  std::string* out_error_message) {
  if (!managed_agent) {
    if (out_error_message) {
      *out_error_message = "Mounted pipeline agent is unavailable.";
    }
    return false;
  }
  if (!out_pipeline_name) {
    if (out_error_message) {
      *out_error_message = "Mounted pipeline name output pointer is null.";
    }
    return false;
  }

  out_pipeline_name->clear();
  if (out_pipeline_index) {
    *out_pipeline_index = 0u;
  }

  size_t matched_index = 0u;
  bool matched = false;
  for (size_t i = 0u; i < agent_hooker::AlgorithmCount(*managed_agent); ++i) {
    const agentmanager::agent::AlgorithmObject* object = agent_hooker::AlgorithmObjectAt(*managed_agent, i);
    if (!object ||
        !object->pipeline_stage ||
        object->pipeline_stage_index != 0u ||
        object->pipeline_topology != agentmanager::agent::AlgorithmPipelineTopology::Circular ||
        _IsPipelineResourceBatchSubmissionName(object->pipeline_name)) {
      continue;
    }

    algorithmManager::JobsPipelineRegistration registration{};
    if (!_TryGetMountedPipelineRegistration(*object, &registration)) {
      DEBUG_TOOL_ASSERT(false, "Mounted pipeline registration is unavailable.");
      if (out_error_message) {
        *out_error_message = "Mounted pipeline registration is unavailable.";
      }
      return false;
    }
    if (registration.root_stage_name != pipeline_algorithm_name) {
      continue;
    }
    if (matched) {
      if (out_error_message) {
        *out_error_message =
          "Multiple mounted pipeline instances match the selected algorithm. Reset to a single mounted pipeline first.";
      }
      return false;
    }
    matched = true;
    matched_index = i;
    *out_pipeline_name = object->pipeline_name;
  }

  if (!matched) {
    if (out_error_message) {
      *out_error_message = "Mount the pipeline before submitting a resource batch.";
    }
    return false;
  }

  if (out_pipeline_index) {
    *out_pipeline_index = matched_index;
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

std::vector<debug_tool::AlgorithmResourceBinding> _ToDebugToolResourceBindings(
  const std::vector<agentmanager::agent::AlgorithmResourceBinding>& bindings) {
  std::vector<debug_tool::AlgorithmResourceBinding> result;
  result.reserve(bindings.size());
  for (const agentmanager::agent::AlgorithmResourceBinding& binding : bindings) {
    result.push_back(debug_tool::AlgorithmResourceBinding{
      .resource_name = binding.resource_name,
      .resource_kind = binding.resource_kind,
      .source_path = binding.source_path,
      .required = true,
    });
  }
  return result;
}

std::vector<debug_tool::AlgorithmDescriptorValue> _ToDebugToolDescriptorValues(
  const std::vector<agentmanager::agent::AlgorithmDescriptorValue>& values) {
  std::vector<debug_tool::AlgorithmDescriptorValue> result;
  result.reserve(values.size());
  for (const agentmanager::agent::AlgorithmDescriptorValue& value : values) {
    result.push_back(debug_tool::AlgorithmDescriptorValue{
      .descriptor_name = value.descriptor_name,
      .scalar_value = value.scalar_value,
    });
  }
  return result;
}

bool _BuildPipelineStageSubmissionsFromPackage(
  const std::string& pipeline_algorithm_name,
  const std::vector<debug_tool::AlgorithmResourceBinding>& stage0_resource_bindings,
  const std::vector<debug_tool::AlgorithmDescriptorValue>& stage0_descriptor_values,
  bool include_stage0_bindings,
  debug_tool::AlgorithmExecutionPreference execution_preference,
  std::vector<debug_tool::AlgorithmPipelineStageSubmission>* out_stage_submissions,
  std::string* out_error_message) {
  if (!out_stage_submissions) {
    if (out_error_message) {
      *out_error_message = "Pipeline stage submission output pointer is null.";
    }
    return false;
  }

  out_stage_submissions->clear();
  if (pipeline_algorithm_name.empty()) {
    if (out_error_message) {
      *out_error_message = "Pipeline algorithm name must not be empty.";
    }
    return false;
  }

  ::algorithm::AlgorithmPackageLocation package_location{};
  std::string location_error_message;
  if (!algorithm_manager_hooker::TryResolveAlgorithmPackageLocation(
        pipeline_algorithm_name,
        &package_location,
        &location_error_message)) {
    if (out_error_message) {
      *out_error_message = location_error_message.empty()
        ? ("Failed to resolve algorithm package location for '" + pipeline_algorithm_name + "'.")
        : std::move(location_error_message);
    }
    return false;
  }

  std::shared_ptr<algorithm::AlgorithmRuntimeTransferMap> transfer_map{};
  bool has_transfer_map = false;
  std::string transfer_map_error_message;
  if (!algorithm_manager_hooker::LoadAlgorithmPackageTransferMapFromLocation(
        package_location,
        &transfer_map,
        &has_transfer_map,
        &transfer_map_error_message)) {
    if (out_error_message) {
      *out_error_message = transfer_map_error_message.empty()
        ? ("Failed to load runtime transfer map for '" + pipeline_algorithm_name + "'.")
        : std::move(transfer_map_error_message);
    }
    return false;
  }

  algorithmManager::catalog::AlgorithmPipelineWrapperSpec wrapper_spec{};
  std::string wrapper_error_message;
  if (!algorithm_manager_hooker::LoadAlgorithmPipelineWrapperSpecFromLocation(
        package_location,
        &wrapper_spec,
        &wrapper_error_message)) {
    if (out_error_message) {
      *out_error_message = wrapper_error_message.empty()
        ? ("Failed to load wrapper spec for '" + pipeline_algorithm_name + "'.")
        : std::move(wrapper_error_message);
    }
    return false;
  }
  if (!wrapper_spec.declared ||
      wrapper_spec.stage_begin.algorithm_name.empty() ||
      wrapper_spec.stage_end.algorithm_name.empty()) {
    if (out_error_message) {
      *out_error_message =
        "Pipeline algorithm must declare wrapper.stage.stageBegin and wrapper.stage.stageEnd: " +
        pipeline_algorithm_name;
    }
    return false;
  }

  if (!has_transfer_map || !transfer_map || transfer_map->empty()) {
    if (out_error_message) {
      *out_error_message =
        "Pipeline algorithm must declare runtime.pipeline mappings: " + pipeline_algorithm_name;
    }
    return false;
  }

  out_stage_submissions->push_back(debug_tool::AlgorithmPipelineStageSubmission{
    .stage_name = pipeline_algorithm_name,
    .resource_bindings = include_stage0_bindings ? stage0_resource_bindings : std::vector<debug_tool::AlgorithmResourceBinding>{},
    .descriptor_values = include_stage0_bindings ? stage0_descriptor_values : std::vector<debug_tool::AlgorithmDescriptorValue>{},
    .execution_preference = execution_preference,
  });

  std::unordered_set<std::string> visited_stage_names{};
  visited_stage_names.insert(pipeline_algorithm_name);
  std::string stage_name = pipeline_algorithm_name;
  while (true) {
    const std::vector<const algorithm::AlgorithmRuntimeTransferEdge*> outgoing_edges =
      transfer_map->FindOutgoingEdges(stage_name);
    if (outgoing_edges.empty()) {
      break;
    }
    if (outgoing_edges.size() != 1u || !outgoing_edges.front()) {
      if (out_error_message) {
        *out_error_message = "Runtime transfer map for '" + pipeline_algorithm_name + "' is not linear.";
      }
      return false;
    }

    const std::string next_stage_name = outgoing_edges.front()->target_stage_name;
    if (next_stage_name.empty()) {
      if (out_error_message) {
        *out_error_message = "Runtime transfer map for '" + pipeline_algorithm_name + "' contains an empty stage name.";
      }
      return false;
    }
    if (!visited_stage_names.insert(next_stage_name).second) {
      if (out_error_message) {
        *out_error_message = "Runtime transfer map for '" + pipeline_algorithm_name + "' contains a cycle at '" +
          next_stage_name + "'.";
      }
      return false;
    }
    ::algorithm::AlgorithmPackageLocation next_package_location{};
    std::string next_location_error_message;
    if (!algorithm_manager_hooker::TryResolveAlgorithmPackageLocation(
          next_stage_name,
          &next_package_location,
          &next_location_error_message)) {
      if (out_error_message) {
        *out_error_message = next_location_error_message.empty()
          ? ("Failed to resolve algorithm package location for '" + next_stage_name + "'.")
          : std::move(next_location_error_message);
      }
      return false;
    }
    std::vector<agentmanager::agent::AlgorithmResourceBinding> default_resource_bindings{};
    std::vector<agentmanager::agent::AlgorithmDescriptorValue> default_descriptor_values{};
    bool has_default_file = false;
    std::string default_error_message;
    if (!algorithm_manager_hooker::LoadAlgorithmPackageDefaultBindingsFromLocation(
          next_package_location,
          &default_resource_bindings,
          &default_descriptor_values,
          &has_default_file,
          &default_error_message)) {
      if (out_error_message) {
        *out_error_message = default_error_message.empty()
          ? ("Failed to load default bindings for '" + next_stage_name + "'.")
          : std::move(default_error_message);
      }
      return false;
    }

    out_stage_submissions->push_back(debug_tool::AlgorithmPipelineStageSubmission{
      .stage_name = next_stage_name,
      .resource_bindings = _ToDebugToolResourceBindings(default_resource_bindings),
      .descriptor_values = _ToDebugToolDescriptorValues(default_descriptor_values),
      .execution_preference = execution_preference,
    });
    stage_name = next_stage_name;
  }

  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool _CollectPipelineStageSubmissionsFromSummary(
  const debug_tool::AgentRuntimeSummary& agent_summary,
  const std::string& pipeline_name,
  std::vector<debug_tool::AlgorithmPipelineStageSubmission>* out_stage_submissions,
  std::string* out_error_message) {
  if (!out_stage_submissions) {
    if (out_error_message) {
      *out_error_message = "Pipeline stage submission output pointer is null.";
    }
    return false;
  }

  out_stage_submissions->clear();
  if (pipeline_name.empty()) {
    if (out_error_message) {
      *out_error_message = "Pipeline name must not be empty.";
    }
    return false;
  }

  std::vector<const debug_tool::AlgorithmRuntimeSummary*> pipeline_stages;
  for (const debug_tool::AlgorithmRuntimeSummary& algorithm_summary : agent_summary.algorithms) {
    if (!algorithm_summary.pipeline_stage || algorithm_summary.pipeline_name != pipeline_name) {
      continue;
    }
    pipeline_stages.push_back(&algorithm_summary);
  }

  if (pipeline_stages.empty()) {
    if (out_error_message) {
      *out_error_message = "Pipeline group is unavailable for '" + pipeline_name + "'.";
    }
    return false;
  }

  std::sort(
    pipeline_stages.begin(),
    pipeline_stages.end(),
    [](const debug_tool::AlgorithmRuntimeSummary* lhs, const debug_tool::AlgorithmRuntimeSummary* rhs) {
      return lhs && rhs
        ? lhs->pipeline_stage_index < rhs->pipeline_stage_index
        : lhs != nullptr;
    });

  out_stage_submissions->reserve(pipeline_stages.size());
  for (const debug_tool::AlgorithmRuntimeSummary* stage_summary : pipeline_stages) {
    if (!stage_summary) {
      if (out_error_message) {
        *out_error_message = "Pipeline group contains an unreadable stage.";
      }
      return false;
    }
    out_stage_submissions->push_back(debug_tool::AlgorithmPipelineStageSubmission{
      .stage_name = stage_summary->algorithm_name,
      .resource_bindings = stage_summary->resource_bindings,
      .descriptor_values = stage_summary->descriptor_values,
      .execution_preference = stage_summary->execution_preference,
    });
  }

  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool _IsPipelineAlgorithmByName(
  const std::string& algorithm_name,
  bool* out_is_pipeline,
  std::string* out_error_message) {
  if (!out_is_pipeline) {
    if (out_error_message) {
      *out_error_message = "Pipeline query output pointer is null.";
    }
    return false;
  }

  *out_is_pipeline = false;
  ::algorithm::AlgorithmPackageLocation package_location{};
  std::string location_error_message;
  if (!algorithm_manager_hooker::TryResolveAlgorithmPackageLocation(
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

  const std::filesystem::path package_root =
    package_location.source_package_root.empty()
      ? package_location.runtime_package_root
      : package_location.source_package_root;
  if (package_root.empty()) {
    if (out_error_message) {
      *out_error_message = "Failed to resolve algorithm runtime package root.";
    }
    return false;
  }

  std::filesystem::path cursor = package_root;
  while (!cursor.empty()) {
    const std::string folder_name = cursor.filename().string();
    if (folder_name == "pipeline") {
      *out_is_pipeline = true;
      if (out_error_message) {
        out_error_message->clear();
      }
      return true;
    }
    if (folder_name == "norm") {
      *out_is_pipeline = false;
      if (out_error_message) {
        out_error_message->clear();
      }
      return true;
    }
    const std::filesystem::path parent = cursor.parent_path();
    if (parent == cursor) {
      break;
    }
    cursor = parent;
  }

  if (out_error_message) {
    *out_error_message = "Algorithm package is not under the norm or pipeline folder: " +
      package_root.generic_string();
  }
  return false;
}

bool _LoadPipelineCompositionByName(
  const std::string& pipeline_algorithm_name,
  debug_tool::AlgorithmPipelineCompositionSummary* out_summary,
  std::string* out_error_message) {
  if (!out_summary) {
    if (out_error_message) {
      *out_error_message = "Pipeline composition output pointer is null.";
    }
    return false;
  }

  out_summary->Clear();
  if (pipeline_algorithm_name.empty()) {
    if (out_error_message) {
      *out_error_message = "Pipeline algorithm name must not be empty.";
    }
    return false;
  }

  ::algorithm::AlgorithmPackageLocation package_location{};
  std::string location_error_message;
  if (!algorithm_manager_hooker::TryResolveAlgorithmPackageLocation(
        pipeline_algorithm_name,
        &package_location,
        &location_error_message)) {
    if (out_error_message) {
      *out_error_message = location_error_message.empty()
        ? ("Failed to resolve pipeline package for '" + pipeline_algorithm_name + "'.")
        : std::move(location_error_message);
    }
    return false;
  }

  std::shared_ptr<algorithm::AlgorithmRuntimeTransferMap> transfer_map{};
  bool has_transfer_map = false;
  std::string transfer_map_error_message;
  if (!algorithm_manager_hooker::LoadAlgorithmPackageTransferMapFromLocation(
        package_location,
        &transfer_map,
        &has_transfer_map,
        &transfer_map_error_message) ||
      !has_transfer_map ||
      !transfer_map ||
      transfer_map->empty()) {
    if (out_error_message) {
      *out_error_message = transfer_map_error_message.empty()
        ? ("Pipeline package has no runtime mapping: " + pipeline_algorithm_name)
        : std::move(transfer_map_error_message);
    }
    return false;
  }

  algorithmManager::catalog::AlgorithmPipelineWrapperSpec wrapper_spec{};
  std::string wrapper_error_message;
  if (!algorithm_manager_hooker::LoadAlgorithmPipelineWrapperSpecFromLocation(
        package_location,
        &wrapper_spec,
        &wrapper_error_message) ||
      !wrapper_spec.declared ||
      wrapper_spec.stage_begin.algorithm_name.empty() ||
      wrapper_spec.stage_end.algorithm_name.empty()) {
    if (out_error_message) {
      *out_error_message = wrapper_error_message.empty()
        ? ("Pipeline package must declare stageBegin and stageEnd: " + pipeline_algorithm_name)
        : std::move(wrapper_error_message);
    }
    return false;
  }

  out_summary->pipeline_name = pipeline_algorithm_name;
  out_summary->stage_begin_name = wrapper_spec.stage_begin.algorithm_name;
  out_summary->stage_end_name = wrapper_spec.stage_end.algorithm_name;
  out_summary->supports_circular_tick = transfer_map->SupportsCircularTick();

  std::unordered_set<std::string> visited_stage_names{};
  std::string stage_name = pipeline_algorithm_name;
  uint32_t stage_index = 0u;
  while (true) {
    if (!visited_stage_names.insert(stage_name).second) {
      if (out_error_message) {
        *out_error_message =
          "Pipeline composition contains a repeated stage: " + stage_name;
      }
      out_summary->Clear();
      return false;
    }

    out_summary->body_stages.push_back(debug_tool::AlgorithmPipelineCompositionStage{
      .stage_name = stage_name,
      .stage_index = stage_index,
    });

    const std::vector<const algorithm::AlgorithmRuntimeTransferEdge*> outgoing_edges =
      transfer_map->FindOutgoingEdges(stage_name);
    if (outgoing_edges.empty()) {
      break;
    }
    if (outgoing_edges.size() != 1u || !outgoing_edges.front()) {
      if (out_error_message) {
        *out_error_message =
          "Pipeline composition is not linear at stage: " + stage_name;
      }
      out_summary->Clear();
      return false;
    }

    const algorithm::AlgorithmRuntimeTransferEdge& edge = *outgoing_edges.front();
    debug_tool::AlgorithmPipelineCompositionEdge composition_edge{};
    composition_edge.source_stage_name = edge.source_stage_name;
    composition_edge.target_stage_name = edge.target_stage_name;
    composition_edge.bindings.reserve(edge.bindings.size());
    for (const algorithm::AlgorithmRuntimeTransferBinding& binding : edge.bindings) {
      composition_edge.bindings.push_back(debug_tool::PipelineStageBridgeDebugBinding{
        .source_stage_name = edge.source_stage_name,
        .target_stage_name = edge.target_stage_name,
        .source_container_name = binding.from_name,
        .target_container_name = binding.to_name,
        .required = binding.required,
      });
    }
    out_summary->edges.push_back(std::move(composition_edge));
    stage_name = edge.target_stage_name;
    ++stage_index;
  }

  out_summary->valid = true;
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

}  // namespace

DebugToolBackendRuntime::~DebugToolBackendRuntime() {
  Destroy();
}

bool DebugToolBackendRuntime::Init(const char* window_title, int width, int height) {
  agent_manager_.Destroy();
  ui_status_message_.clear();
  agent_manager_.SetAlgorithmLibraryRuntimeBuildFlavor(
    static_cast<algorithm::library_paths::AlgorithmLibraryRuntimeBuildFlavor>(
      debug_tool::AlgorithmRuntimeBuildFlavor::Debug));
  try {
    if (!runtime_environment_.Init(window_title ? window_title : "debugTool", width, height)) {
      return false;
    }
  } catch (const std::exception& e) {
    throw;
  } catch (...) {
    throw;
  }

  AgentCreateSpec default_agent_spec{};
  default_agent_spec.agent_name = "debug_agent";
  default_agent_spec.limit_fps_flag = common_data::DefaultAgentLimitFpsFlag();
  if (!CreateAgent(default_agent_spec.agent_name.c_str(), default_agent_spec.limit_fps_flag)) {
    runtime_environment_.Destroy();
    return false;
  }

  PauseTicking();
  frame_dt_ = 0.0f;
  last_frame_time_ = std::chrono::steady_clock::now();
  return true;
}

bool DebugToolBackendRuntime::Tick() {
  const auto now = std::chrono::steady_clock::now();
  frame_dt_ = std::chrono::duration<float>(now - last_frame_time_).count();
  last_frame_time_ = now;
  const uint64_t tick_sequence = ++tick_sequence_;

  if (!agent_manager_.Tick(
        runtime_environment_.input(),
        runtime_environment_.MousePosition(),
        frame_dt_,
        render_preview_extent_)) {
    std::cerr << "backend_tick.agent_failed sequence=" << tick_sequence << '\n';
    return false;
  }
  const bool runtime_tick_ok = runtime_environment_.Tick();
  return runtime_tick_ok;
}

void DebugToolBackendRuntime::Destroy() {
  agent_manager_.Destroy();
  runtime_environment_.Destroy();
  ui_status_message_.clear();
  last_frame_time_ = {};
  frame_dt_ = 0.0f;
  tick_sequence_ = 0u;
}

bool DebugToolBackendRuntime::CreateAgent(const char* agent_name, uint32_t limit_fps_flag, size_t* out_agent_index) {
  AgentCreateSpec spec{};
  spec.agent_name = agent_name ? agent_name : "";
  spec.limit_fps_flag = limit_fps_flag;
  return agent_manager_.CreateAgent(std::move(spec), out_agent_index);
}

bool DebugToolBackendRuntime::has_agents() const {
  return agent_manager_.has_agents();
}

size_t DebugToolBackendRuntime::agent_count() const {
  return agent_manager_.agent_count();
}

bool DebugToolBackendRuntime::AttachAlgorithmToAgent(
  size_t agent_index,
  const std::string& algorithm_name,
  const std::vector<debug_tool::AlgorithmResourceBinding>& resource_bindings,
  const std::vector<debug_tool::AlgorithmDescriptorValue>& descriptor_values,
  size_t* out_algorithm_index,
  std::string* out_error_message,
  debug_tool::AlgorithmMountMode mount_mode,
  debug_tool::AlgorithmExecutionPreference execution_preference) {
  if (mount_mode == debug_tool::AlgorithmMountMode::Pipeline) {
    if (out_error_message) {
      *out_error_message =
        "Pipeline algorithms must be mounted through the dedicated pipeline path, not AttachAlgorithmToAgent.";
    }
    DEBUG_TOOL_ASSERT(false, "Pipeline mount was routed through the normal algorithm attach path.");
    return false;
  }

  bool is_pipeline = false;
  std::string pipeline_error_message;
  if (!algorithm_name.empty() &&
      !IsPipelineAlgorithm(algorithm_name, &is_pipeline, &pipeline_error_message)) {
    if (out_error_message) {
      *out_error_message = pipeline_error_message.empty()
        ? ("Failed to resolve algorithm type for '" + algorithm_name + "'.")
        : std::move(pipeline_error_message);
    }
    DEBUG_TOOL_ASSERT(false, "Failed to resolve algorithm type for algorithm attachment.");
    return false;
  }

  if (is_pipeline) {
    if (out_error_message) {
      *out_error_message =
        "Pipeline algorithms must use pipeline mount + resource submission, not the normal algorithm attach path.";
    }
    DEBUG_TOOL_ASSERT(false, "Pipeline algorithm was submitted through the normal algorithm attach path.");
    return false;
  }

  const bool load_reflector = true;
  const bool attached = agent_manager_.AttachAlgorithmToAgent(
    agent_index,
    algorithm_name,
    _ToAgentResourceBindings(resource_bindings),
    _ToAgentDescriptorValues(descriptor_values),
    out_algorithm_index,
    out_error_message,
    static_cast<agentmanager::agent::AlgorithmMountMode>(static_cast<int>(mount_mode)),
    static_cast<agentmanager::agent::AlgorithmExecutionPreference>(static_cast<int>(execution_preference)),
    load_reflector);
  if (!attached && out_error_message && out_error_message->empty()) {
    *out_error_message = "Failed to attach algorithm to the built-in agent.";
  }
  return attached;
}

bool DebugToolBackendRuntime::AttachPipelinePackageToAgent(
  size_t agent_index,
  const std::string& pipeline_name,
  const std::string& pipeline_algorithm_name,
  const std::vector<debug_tool::AlgorithmResourceBinding>& resource_bindings,
  const std::vector<debug_tool::AlgorithmDescriptorValue>& descriptor_values,
  size_t* out_algorithm_index,
  std::string* out_error_message,
  debug_tool::AlgorithmExecutionPreference execution_preference) {
  const bool include_stage0_bindings = _IsPipelineResourceBatchSubmissionName(pipeline_name);
  if (include_stage0_bindings) {
    const std::shared_ptr<agentmanager::agent::Agent> managed_agent = agent_manager_.agent(agent_index);
    std::string mounted_pipeline_name{};
    size_t mounted_pipeline_index = 0u;
    if (!_FindMountedPipelineInstanceName(
          managed_agent,
          pipeline_algorithm_name,
          &mounted_pipeline_name,
          &mounted_pipeline_index,
          out_error_message)) {
      DEBUG_TOOL_ASSERT(false, "Failed to resolve the mounted pipeline instance for resource submission.");
      return false;
    }
    if (!agent_manager_.EnqueuePipelineStage0Submission(
          agent_index,
          mounted_pipeline_name,
          _ToAgentResourceBindings(resource_bindings),
          _ToAgentDescriptorValues(descriptor_values),
          out_error_message)) {
      DEBUG_TOOL_ASSERT(false, "Failed to enqueue the pipeline resource batch.");
      return false;
    }
    if (out_algorithm_index) {
      *out_algorithm_index = mounted_pipeline_index;
    }
    return true;
  }

  std::vector<debug_tool::AlgorithmPipelineStageSubmission> stage_submissions{};
  if (!_BuildPipelineStageSubmissionsFromPackage(
        pipeline_algorithm_name,
        resource_bindings,
        descriptor_values,
        include_stage0_bindings,
        execution_preference,
        &stage_submissions,
        out_error_message)) {
    DEBUG_TOOL_ASSERT(false, "Failed to expand pipeline stage submissions.");
    return false;
  }

  size_t attached_algorithm_index = 0u;
  const bool attached = agent_manager_.AttachPipelineAlgorithmToAgent(
    agent_index,
    pipeline_name,
    _ToAgentPipelineStageSubmissions(stage_submissions),
    &attached_algorithm_index,
    out_error_message,
    static_cast<agentmanager::agent::AlgorithmExecutionPreference>(static_cast<int>(execution_preference)),
    agentmanager::agent::AlgorithmPipelineTopology::Circular,
    agentmanager::agent::AlgorithmPipelineSyncMode::Forced);
  if (!attached) {
    return false;
  }

  if (out_algorithm_index) {
    *out_algorithm_index = attached_algorithm_index;
  }

  if (!include_stage0_bindings) {
    const std::shared_ptr<agentmanager::agent::Agent> managed_agent = agent_manager_.agent(agent_index);
    DEBUG_TOOL_ASSERT(managed_agent != nullptr, "Mounted pipeline agent is unavailable.");
    if (!managed_agent) {
      if (out_error_message) {
        *out_error_message = "Mounted pipeline agent is unavailable.";
      }
      return false;
    }
    const agentmanager::agent::AlgorithmObject* root_stage =
      agent_hooker::AlgorithmObjectAt(*managed_agent, attached_algorithm_index);
    DEBUG_TOOL_ASSERT(root_stage != nullptr, "Mounted pipeline root stage is unavailable.");
    if (!root_stage) {
      if (out_error_message) {
        *out_error_message = "Mounted pipeline root stage is unavailable.";
      }
      return false;
    }
    const bool marked_waiting = agent_hooker::BeginAlgorithmAssembly(*managed_agent, attached_algorithm_index);
    DEBUG_TOOL_ASSERT(
      marked_waiting,
      "Failed to mark mounted pipeline node as waiting for resource submission.");
    if (!marked_waiting) {
      if (out_error_message) {
        *out_error_message = "Failed to mark mounted pipeline node as waiting for resource submission.";
      }
      return false;
    }
  }
  return attached;
}

bool DebugToolBackendRuntime::EnqueuePipelineStage0Submission(
  size_t agent_index,
  const std::string& pipeline_name,
  const std::vector<debug_tool::AlgorithmResourceBinding>& resource_bindings,
  const std::vector<debug_tool::AlgorithmDescriptorValue>& descriptor_values,
  std::string* out_error_message) {
  return agent_manager_.EnqueuePipelineStage0Submission(
    agent_index,
    pipeline_name,
    _ToAgentResourceBindings(resource_bindings),
    _ToAgentDescriptorValues(descriptor_values),
    out_error_message);
}

bool DebugToolBackendRuntime::IsPipelineAlgorithm(
  const std::string& algorithm_name,
  bool* out_is_pipeline,
  std::string* out_error_message) const {
  return _IsPipelineAlgorithmByName(algorithm_name, out_is_pipeline, out_error_message);
}

bool DebugToolBackendRuntime::LoadPipelineComposition(
  const std::string& pipeline_algorithm_name,
  debug_tool::AlgorithmPipelineCompositionSummary* out_summary,
  std::string* out_error_message) const {
  return _LoadPipelineCompositionByName(
    pipeline_algorithm_name,
    out_summary,
    out_error_message);
}

void DebugToolBackendRuntime::SetAlgorithmRuntimeBuildFlavor(
  debug_tool::AlgorithmRuntimeBuildFlavor build_flavor) {
  agent_manager_.SetAlgorithmLibraryRuntimeBuildFlavor(
    static_cast<algorithm::library_paths::AlgorithmLibraryRuntimeBuildFlavor>(build_flavor));
}

bool DebugToolBackendRuntime::RequestAgentTimingLog(
  size_t agent_index,
  std::string* out_error_message) {
  return agent_manager_.RequestAgentTimingLog(agent_index, out_error_message);
}

bool DebugToolBackendRuntime::ExportPipelineTimingArtifacts(
  size_t agent_index,
  const std::string& pipeline_name,
  std::string* out_csv_path,
  std::string* out_mermaid_path,
  std::string* out_error_message) const {
  debug_tool::AgentRuntimeSummary agent_summary{};
  if (!GetAgentSummary(agent_index, &agent_summary)) {
    if (out_error_message) {
      *out_error_message = "Selected agent is unavailable.";
    }
    return false;
  }

  const debug_tool::AlgorithmRuntimeSummary* pipeline_summary = nullptr;
  for (const debug_tool::AlgorithmRuntimeSummary& algorithm_summary : agent_summary.algorithms) {
    if (!algorithm_summary.pipeline_stage || algorithm_summary.pipeline_stage_index != 0u) {
      continue;
    }
    if (!pipeline_name.empty() && algorithm_summary.pipeline_name != pipeline_name) {
      continue;
    }
    pipeline_summary = &algorithm_summary;
    break;
  }
  if (!pipeline_summary) {
    if (out_error_message) {
      *out_error_message = "Pipeline timing source is unavailable.";
    }
    return false;
  }
  if (pipeline_summary->pipeline_stage_runtime_stats.empty()) {
    if (out_error_message) {
      *out_error_message = "Pipeline timing has not been collected for the selected frame.";
    }
    return false;
  }

  agentmanager::AlgorithmPipelineStallReport report{};
  report.algorithm_name = pipeline_summary->pipeline_name.empty()
    ? pipeline_summary->algorithm_name
    : pipeline_summary->pipeline_name;
  report.reason = "debug command one-frame pipeline timing";
  report.stage_runtime_stats = pipeline_summary->pipeline_stage_runtime_stats;
  return agentmanager::ExportAlgorithmPipelineTimingArtifacts(
    report,
    out_csv_path,
    out_mermaid_path,
    out_error_message);
}

bool DebugToolBackendRuntime::AttachPipelineAlgorithmToAgent(
  size_t agent_index,
  const std::string& pipeline_name,
  const std::vector<debug_tool::AlgorithmPipelineStageSubmission>& stage_submissions,
  size_t* out_algorithm_index,
  std::string* out_error_message,
  debug_tool::AlgorithmExecutionPreference execution_preference) {
  const bool load_reflector = true;
  const bool attached = agent_manager_.AttachPipelineAlgorithmToAgent(
    agent_index,
    pipeline_name,
    _ToAgentPipelineStageSubmissions(stage_submissions),
    out_algorithm_index,
    out_error_message,
    static_cast<agentmanager::agent::AlgorithmExecutionPreference>(static_cast<int>(execution_preference)),
    agentmanager::agent::AlgorithmPipelineTopology::NonCircular,
    agentmanager::agent::AlgorithmPipelineSyncMode::Forced,
    load_reflector);
  if (!attached) {
    DEBUG_TOOL_ASSERT(false, "Failed to attach pipeline algorithm to the built-in agent.");
  }
  return attached;
}

bool DebugToolBackendRuntime::ReplayPipelineStageBridgeDebug(
  size_t agent_index,
  size_t algorithm_index,
  std::string* out_error_message) {
  const agentmanager::agent::AgentTickContext context{
    .input = &runtime_environment_.input(),
    .mouse_pixel = runtime_environment_.MousePosition(),
    .render_preview_extent = render_preview_extent_,
    .dt_seconds = frame_dt_,
    .intervention_request = nullptr,
  };
  return agent_manager_.ReplayPipelineStageBridgeDebug(
    agent_index,
    algorithm_index,
    context,
    out_error_message);
}

bool DebugToolBackendRuntime::HotReloadAlgorithmPackage(
  size_t agent_index,
  size_t algorithm_index,
  size_t* out_algorithm_index,
  std::string* out_error_message) {
  if (out_algorithm_index) {
    *out_algorithm_index = 0u;
  }

  debug_tool::AgentRuntimeSummary agent_summary{};
  if (!GetAgentSummary(agent_index, &agent_summary)) {
    if (out_error_message) {
      *out_error_message = "Selected agent is unavailable.";
    }
    return false;
  }
  if (algorithm_index >= agent_summary.algorithms.size()) {
    if (out_error_message) {
      *out_error_message = "Selected algorithm is unavailable.";
    }
    return false;
  }

  const debug_tool::AlgorithmRuntimeSummary algorithm_summary =
    agent_summary.algorithms[algorithm_index];
  if (algorithm_summary.algorithm_name.empty()) {
    if (out_error_message) {
      *out_error_message = "Selected algorithm has no name.";
    }
    return false;
  }

  const bool pipeline_algorithm = algorithm_summary.pipeline_stage && !algorithm_summary.pipeline_name.empty();
  std::shared_ptr<algorithm::AlgorithmContainerSet> cached_container_set{};
  const agentmanager::agent::AlgorithmMountMode target_mount_mode = pipeline_algorithm
    ? agentmanager::agent::AlgorithmMountMode::Direct
    : static_cast<agentmanager::agent::AlgorithmMountMode>(static_cast<int>(algorithm_summary.mount_mode));
  if (pipeline_algorithm) {
    const std::shared_ptr<agentmanager::agent::Agent> managed_agent = agent_manager_.agent(agent_index);
    const agentmanager::agent::AlgorithmObject* selected_object =
      managed_agent ? agent_hooker::AlgorithmObjectAt(*managed_agent, algorithm_index) : nullptr;
    if (!selected_object || !selected_object->container_set()) {
      if (out_error_message) {
        *out_error_message = "Selected pipeline stage container set is unavailable.";
      }
      return false;
    }

    cached_container_set = std::make_shared<algorithm::AlgorithmContainerSet>();
    algorithm::CopyAlgorithmContainerSet(*selected_object->container_set(), cached_container_set.get());
  }

  if (pipeline_algorithm) {
    const std::shared_ptr<agentmanager::agent::Agent> managed_agent = agent_manager_.agent(agent_index);
    if (!managed_agent) {
      if (out_error_message) {
        *out_error_message = "Selected agent is unavailable.";
      }
      return false;
    }
  }

  const bool was_ticking = agent_manager_.tick_enabled();
  if (was_ticking) {
    agent_manager_.PauseTicking();
  }

  std::string detach_error_message;
  if (!agent_manager_.DetachAlgorithmFromAgent(agent_index, algorithm_index, &detach_error_message)) {
    if (was_ticking) {
      agent_manager_.StartTicking();
    }
    if (out_error_message) {
      *out_error_message = detach_error_message.empty()
        ? ("Failed to detach algorithm '" + algorithm_summary.algorithm_name + "'.")
        : std::move(detach_error_message);
    }
    return false;
  }

  runtime_environment_.SetRenderPreviewRequest({});
  runtime_environment_.ClearVkRuntimeCaches();

  const std::string build_command = _HotReloadBuildCommand(algorithm_summary.algorithm_name);
  const int build_result = std::system(build_command.c_str());
  if (build_result != 0) {
    size_t restored_algorithm_index = 0u;
    std::string restore_error_message;
    const bool restored = agent_manager_.AttachAlgorithmToAgent(
      agent_index,
      algorithm_summary.algorithm_name,
      _ToAgentResourceBindings(algorithm_summary.resource_bindings),
      _ToAgentDescriptorValues(algorithm_summary.descriptor_values),
      &restored_algorithm_index,
      &restore_error_message,
      target_mount_mode,
      static_cast<agentmanager::agent::AlgorithmExecutionPreference>(static_cast<int>(algorithm_summary.execution_preference)),
      algorithmManager::GetAlgorithmLibraryRuntimeBuildFlavor() !=
        algorithm::library_paths::AlgorithmLibraryRuntimeBuildFlavor::ReleaseWithDebugInfo);
    if (!restored) {
      if (out_error_message) {
        *out_error_message = restore_error_message.empty()
          ? ("Hot reload failed and the old algorithm could not be restored for '" +
            algorithm_summary.algorithm_name + "'.")
          : std::move(restore_error_message);
      }
    } else if (out_error_message) {
      *out_error_message =
        "Hot reload build failed for '" + algorithm_summary.algorithm_name + "'.";
    }

    if (restored && pipeline_algorithm) {
      const std::shared_ptr<agentmanager::agent::Agent> managed_agent = agent_manager_.agent(agent_index);
      agentmanager::agent::AlgorithmObject* restored_object =
        managed_agent ? managed_agent->algorithm_object(restored_algorithm_index) : nullptr;
      if (restored_object && cached_container_set) {
        restored_object->SetContainerSet(std::move(cached_container_set));
        if (agentmanager::agent::AgentAlgorithmRuntimeState* runtime_state =
              managed_agent->algorithm_runtime_state(restored_algorithm_index)) {
          agentmanager::agent::AlgorithmReflectionSnapshot reflection_snapshot{};
          if (managed_agent->CollectAlgorithmReflection(restored_algorithm_index, &reflection_snapshot)) {
            runtime_state->reflection_snapshot = std::move(reflection_snapshot);
            runtime_state->reflection_snapshot_cached = true;
          }
        }
      }
    }

    runtime_environment_.SetRenderPreviewRequest({});
    runtime_environment_.ClearVkRuntimeCaches();

    if (out_algorithm_index) {
      *out_algorithm_index = restored_algorithm_index;
    }
    if (was_ticking) {
      agent_manager_.StartTicking();
    }
    return false;
  }

  size_t rebuilt_algorithm_index = 0u;
  std::string attach_error_message;
  const bool rebuilt = agent_manager_.AttachAlgorithmToAgent(
    agent_index,
    algorithm_summary.algorithm_name,
    _ToAgentResourceBindings(algorithm_summary.resource_bindings),
    _ToAgentDescriptorValues(algorithm_summary.descriptor_values),
    &rebuilt_algorithm_index,
    &attach_error_message,
    target_mount_mode,
    static_cast<agentmanager::agent::AlgorithmExecutionPreference>(static_cast<int>(algorithm_summary.execution_preference)),
    algorithmManager::GetAlgorithmLibraryRuntimeBuildFlavor() !=
      algorithm::library_paths::AlgorithmLibraryRuntimeBuildFlavor::ReleaseWithDebugInfo);
  if (!rebuilt) {
    if (out_error_message) {
      *out_error_message = attach_error_message.empty()
        ? ("Hot reload succeeded, but the updated algorithm could not be reattached for '" +
          algorithm_summary.algorithm_name + "'.")
        : std::move(attach_error_message);
    }
    runtime_environment_.SetRenderPreviewRequest({});
    runtime_environment_.ClearVkRuntimeCaches();
    if (was_ticking) {
      agent_manager_.StartTicking();
    }
    return false;
  }

  if (pipeline_algorithm && cached_container_set) {
    const std::shared_ptr<agentmanager::agent::Agent> managed_agent = agent_manager_.agent(agent_index);
    agentmanager::agent::AlgorithmObject* rebuilt_object =
      managed_agent ? managed_agent->algorithm_object(rebuilt_algorithm_index) : nullptr;
    if (rebuilt_object) {
      rebuilt_object->SetContainerSet(std::move(cached_container_set));
      if (agentmanager::agent::AgentAlgorithmRuntimeState* runtime_state =
            managed_agent->algorithm_runtime_state(rebuilt_algorithm_index)) {
        agentmanager::agent::AlgorithmReflectionSnapshot reflection_snapshot{};
        if (managed_agent->CollectAlgorithmReflection(rebuilt_algorithm_index, &reflection_snapshot)) {
          runtime_state->reflection_snapshot = std::move(reflection_snapshot);
          runtime_state->reflection_snapshot_cached = true;
        }
      }
    }
  }

  runtime_environment_.SetRenderPreviewRequest({});
  runtime_environment_.ClearVkRuntimeCaches();

  if (out_algorithm_index) {
    *out_algorithm_index = rebuilt_algorithm_index;
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  if (was_ticking) {
    agent_manager_.StartTicking();
  }
  return true;
}

bool DebugToolBackendRuntime::GetAgentSummary(
  size_t agent_index,
  debug_tool::AgentRuntimeSummary* out_summary) const {
  if (!out_summary) {
    return false;
  }

  *out_summary = {};

  const std::shared_ptr<agentmanager::agent::Agent> managed_agent = agent_manager_.agent(agent_index);
  if (!managed_agent) {
    return false;
  }

  out_summary->agent_name = agent_hooker::AgentName(*managed_agent);
  out_summary->algorithms.reserve(agent_hooker::AlgorithmCount(*managed_agent));
  for (size_t algorithm_index = 0; algorithm_index < agent_hooker::AlgorithmCount(*managed_agent); ++algorithm_index) {
    out_summary->algorithms.push_back(_ToDebugToolAlgorithmRuntimeSummary(*managed_agent, algorithm_index));
  }
  return true;
}

const AlgorithmToAgentSignal& DebugToolBackendRuntime::combined_algorithm_to_agent_signal() const {
  return agent_manager_.combined_algorithm_to_agent_signal();
}

bool DebugToolBackendRuntime::LoadAlgorithmCatalog(
  std::vector<debug_tool::AlgorithmCatalogEntry>* out_entries,
  std::string* out_error_message) const {
  if (!out_entries) {
    if (out_error_message) {
      *out_error_message = "Runtime algorithm entries output pointer is null.";
    }
    return false;
  }

  out_entries->clear();
  const std::filesystem::path runtime_root = algorithm_manager_hooker::ResolveAlgorithmLibraryRuntimeRoot();
  const std::filesystem::path category_roots[] = {
    runtime_root / "norm",
    runtime_root / "pipeline",
  };

  std::error_code ec;
  for (const std::filesystem::path& category_root : category_roots) {
    if (category_root.empty() || !std::filesystem::exists(category_root, ec) || !std::filesystem::is_directory(category_root, ec)) {
      continue;
    }

    for (const std::filesystem::directory_entry& category_entry : std::filesystem::directory_iterator(category_root, ec)) {
      if (ec) {
        break;
      }
      if (!category_entry.is_directory(ec)) {
        continue;
      }

      const std::filesystem::path algorithm_folder_path = category_entry.path();
      const std::string algorithm_name = algorithm_folder_path.filename().string();
      if (algorithm_name.empty()) {
        continue;
      }

      debug_tool::AlgorithmCatalogEntry entry{};
      entry.algorithm_name = algorithm_name;
      entry.display_name = algorithm_name;
      entry.folder_name = algorithm_folder_path.lexically_relative(runtime_root).generic_string();
      out_entries->push_back(std::move(entry));
    }
  }

  std::sort(out_entries->begin(), out_entries->end(), [](const debug_tool::AlgorithmCatalogEntry& lhs, const debug_tool::AlgorithmCatalogEntry& rhs) {
    if (lhs.algorithm_name == rhs.algorithm_name) {
      return lhs.folder_name < rhs.folder_name;
    }
    return lhs.algorithm_name < rhs.algorithm_name;
  });

  if (out_entries->empty()) {
    if (out_error_message) {
      *out_error_message = "No runtime algorithm folders were found under algorithmruntimeLib/norm or algorithmruntimeLib/pipeline.";
    }
    return false;
  }

  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool DebugToolBackendRuntime::QueryAlgorithmRequestedBindings(
  const std::string& algorithm_name,
  std::vector<debug_tool::RequestedResourceEntry>* out_resources,
  std::vector<debug_tool::RequestedDescriptorEntry>* out_descriptors,
  std::string* out_error_message) const {
  if (!out_resources || !out_descriptors) {
    if (out_error_message) {
      *out_error_message = "Requested binding output pointers are null.";
    }
    return false;
  }

  out_resources->clear();
  out_descriptors->clear();

  algorithmManager::AlgorithmRequestedResources requested_resources{};
  algorithmManager::AlgorithmRequestedDescriptorBindings requested_descriptor_bindings{};
  std::string reflection_error_message;
  if (!algorithm_manager_hooker::QueryAlgorithmRequestedBindings(
        algorithm_name,
        &requested_resources,
        &requested_descriptor_bindings,
        &reflection_error_message)) {
    if (out_error_message) {
      *out_error_message = reflection_error_message.empty()
        ? ("Failed to query requested bindings for '" + algorithm_name + "'.")
        : std::move(reflection_error_message);
    }
    return false;
  }

  for (const algorithmManager::AlgorithmRequestedResources::RequiredResource& resource :
        requested_resources.required_resources) {
    if (resource.resource_name.empty()) {
      DEBUG_TOOL_ASSERT(false, "Requested resource entry is missing a resource name.");
      if (out_error_message) {
        *out_error_message =
          "Backend returned an unreadable requested resource entry without a resource name for '" +
          algorithm_name + "'.";
      }
      return false;
    }
    if (resource.resource_kind.empty()) {
      DEBUG_TOOL_ASSERT(false, "Requested resource entry is missing a resource kind.");
      if (out_error_message) {
        *out_error_message =
          "Backend returned an unreadable requested resource entry without a resource kind for '" +
          algorithm_name + "'.";
      }
      return false;
    }
    out_resources->push_back(debug_tool::RequestedResourceEntry{
      .resource_name = resource.resource_name,
      .resource_kind = resource.resource_kind,
      .required = resource.required,
    });
  }
  for (const algorithmManager::AlgorithmRequestedDescriptorBindings::DescriptorSlot& descriptor :
        requested_descriptor_bindings.descriptor_slots) {
    if (descriptor.descriptor_name.empty()) {
      DEBUG_TOOL_ASSERT(false, "Requested descriptor entry is missing a descriptor name.");
      if (out_error_message) {
        *out_error_message =
          "Backend returned an unreadable requested descriptor entry without a descriptor name for '" +
          algorithm_name + "'.";
      }
      return false;
    }
    if (descriptor.container_name.empty()) {
      DEBUG_TOOL_ASSERT(false, "Requested descriptor entry is missing a container name.");
      if (out_error_message) {
        *out_error_message =
          "Backend returned an unreadable requested descriptor entry without a container name for '" +
          algorithm_name + "'.";
      }
      return false;
    }
    out_descriptors->push_back(debug_tool::RequestedDescriptorEntry{
      .descriptor_name = descriptor.descriptor_name,
      .container_name = descriptor.container_name,
      .array_index = descriptor.array_index,
    });
  }

  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool DebugToolBackendRuntime::LoadAlgorithmPackageDefaultBindings(
  const std::string& algorithm_name,
  std::vector<debug_tool::AlgorithmResourceBinding>* out_resource_bindings,
  std::vector<debug_tool::AlgorithmDescriptorValue>* out_descriptor_values,
  bool* out_has_default_file,
  std::string* out_error_message) const {
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

  std::vector<algorithmManager::AlgorithmResourceBinding> package_resource_bindings;
  std::vector<algorithmManager::AlgorithmDescriptorValue> package_descriptor_values;
  bool has_default_file = false;
  std::string default_error_message;
  if (!algorithm_manager_hooker::LoadAlgorithmPackageDefaultBindings(
        algorithm_name,
        &package_resource_bindings,
        &package_descriptor_values,
        &has_default_file,
        &default_error_message)) {
    if (out_error_message) {
      *out_error_message = default_error_message.empty()
        ? ("Failed to load default bindings for '" + algorithm_name + "'.")
        : std::move(default_error_message);
    }
    return false;
  }

  if (!has_default_file) {
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  out_resource_bindings->reserve(package_resource_bindings.size());
  for (const algorithmManager::AlgorithmResourceBinding& binding : package_resource_bindings) {
    out_resource_bindings->push_back(debug_tool::AlgorithmResourceBinding{
      .resource_name = binding.resource_name,
      .resource_kind = binding.resource_kind,
      .source_path = binding.source_path,
      .required = true,
    });
  }
  out_descriptor_values->reserve(package_descriptor_values.size());
  for (const algorithmManager::AlgorithmDescriptorValue& value : package_descriptor_values) {
    out_descriptor_values->push_back(debug_tool::AlgorithmDescriptorValue{
      .descriptor_name = value.descriptor_name,
      .scalar_value = value.scalar_value,
    });
  }
  if (out_has_default_file) {
    *out_has_default_file = true;
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool DebugToolBackendRuntime::BuildRenderPreviewRequest(
  size_t agent_index,
  size_t algorithm_index,
  RenderPreviewRequest* out_request,
  std::string* out_error_message) const {
  if (!out_request) {
    DEBUG_TOOL_ASSERT(false, "Preview request output pointer is null.");
    if (out_error_message) {
      *out_error_message = "Preview request output pointer is null.";
    }
    return false;
  }

  out_request->Clear();

  const std::shared_ptr<agentmanager::agent::Agent> managed_agent = agent_manager_.agent(agent_index);
  if (!managed_agent) {
    DEBUG_TOOL_ASSERT(false, "Managed agent is unavailable.");
    if (out_error_message) {
      *out_error_message = "Managed agent is unavailable.";
    }
    return false;
  }

  const agentmanager::agent::AlgorithmObject* object = agent_hooker::AlgorithmObjectAt(*managed_agent, algorithm_index);
  if (!object) {
    if (out_error_message) {
      *out_error_message = "Algorithm is unavailable for preview rendering.";
    }
    return false;
  }

  size_t preview_source_index = algorithm_index;
  std::vector<agentmanager::agent::AlgorithmPhaseSpec> phase_specs;
  if (!_TryResolveRenderPreviewSource(
        *managed_agent,
        algorithm_index,
        &preview_source_index,
        &phase_specs,
        out_error_message)) {
    return false;
  }

  const agentmanager::agent::AlgorithmObject* preview_object =
    agent_hooker::AlgorithmObjectAt(*managed_agent, preview_source_index);
  if (!preview_object) {
    DEBUG_TOOL_ASSERT(false, "Preview source algorithm object is unavailable.");
    if (out_error_message) {
      *out_error_message = "Preview source algorithm object is unavailable.";
    }
    return false;
  }
  if (!preview_object->child_algorithm_objects.empty()) {
    for (const std::shared_ptr<agentmanager::agent::AlgorithmObject>& child :
         preview_object->child_algorithm_objects) {
      std::vector<agentmanager::agent::AlgorithmPhaseSpec> child_phase_specs;
      if (_TryLoadInterventionPhaseSpecs(*child, &child_phase_specs) &&
          _ContainsResultRenderPhase(child_phase_specs)) {
        preview_object = child.get();
        break;
      }
    }
  }

  if (phase_specs.empty()) {
    if (out_error_message) {
      *out_error_message = "Algorithm intervention did not expose any phases.";
    }
    return false;
  }
  const agentmanager::agent::AlgorithmPhaseSpec* result_phase = nullptr;
  for (const agentmanager::agent::AlgorithmPhaseSpec& phase_spec : phase_specs) {
    if (phase_spec.stage_kind == agentmanager::agent::AlgorithmPhaseKind::ResultRender) {
      result_phase = &phase_spec;
      break;
    }
  }
  if (!result_phase) {
    if (out_error_message) {
      *out_error_message = "Scheduler did not submit any valid renderresult phase.";
    }
    return false;
  }
  if (result_phase->shader.vertex_shader_path.empty() || result_phase->shader.fragment_shader_path.empty()) {
    if (out_error_message) {
      *out_error_message = "Result-render phase is missing shader paths.";
    }
    return false;
  }

  const algorithm::AlgorithmContainerSet* container_set = agent_hooker::ContainerSet(*preview_object);
  if (!container_set) {
    DEBUG_TOOL_ASSERT(false, "Algorithm container set is unavailable for preview rendering.");
    if (out_error_message) {
      *out_error_message = "Algorithm container set is unavailable.";
    }
    return false;
  }

  out_request->execution_key = container_set;
  out_request->stage_name = result_phase->stage_name;
  out_request->vertex_shader_path =
    _ResolveAlgorithmShaderPath(*preview_object, result_phase->shader.vertex_shader_path);
  out_request->fragment_shader_path =
    _ResolveAlgorithmShaderPath(*preview_object, result_phase->shader.fragment_shader_path);
  const std::string vertex_shader_binary_path = _ResolveShaderBinaryPath(out_request->vertex_shader_path);
  const std::string fragment_shader_binary_path = _ResolveShaderBinaryPath(out_request->fragment_shader_path);
  if (!_IsReadableNonEmptyFile(vertex_shader_binary_path)) {
    if (out_error_message) {
      *out_error_message = "Preview vertex shader binary is unavailable: " + vertex_shader_binary_path;
    }
    out_request->Clear();
    return false;
  }
  if (!_IsReadableNonEmptyFile(fragment_shader_binary_path)) {
    if (out_error_message) {
      *out_error_message = "Preview fragment shader binary is unavailable: " + fragment_shader_binary_path;
    }
    out_request->Clear();
    return false;
  }
  out_request->storage_buffers.reserve(result_phase->used_algorithm_containers.size());

  for (const agentmanager::agent::AlgorithmPhaseContainerBinding& binding : result_phase->used_algorithm_containers) {
    const algorithm::AlgorithmContainer* container = algorithm::FindAlgorithmContainer(*container_set, binding.container_name);
    if (!container) {
      DEBUG_TOOL_ASSERT(false, "Required preview container is missing.");
      if (binding.required) {
        if (out_error_message) {
          *out_error_message = "Missing preview container: " + binding.container_name;
        }
        out_request->Clear();
        return false;
      }
      continue;
    }
    const bool has_container_bytes = !container->bytes.empty();
    if (container->element_stride == 0u || !has_container_bytes) {
      DEBUG_TOOL_ASSERT(false, "Preview container has no drawable data.");
      if (binding.required) {
        if (out_error_message) {
          *out_error_message = "Preview container has no data: " + binding.container_name;
        }
        out_request->Clear();
        return false;
      }
      continue;
    }

    RenderPreviewBuffer preview_buffer{};
    preview_buffer.binding_name = binding.container_name;
    preview_buffer.element_stride = container->element_stride;
    preview_buffer.bytes.assign(container->bytes.begin(), container->bytes.end());
    out_request->storage_buffers.push_back(std::move(preview_buffer));
  }

  if (out_request->storage_buffers.empty()) {
    DEBUG_TOOL_ASSERT(false, "Scheduler did not submit any valid renderresult phase.");
    if (out_error_message) {
      *out_error_message = "Scheduler did not submit any valid renderresult phase.";
    }
    out_request->Clear();
    return false;
  }

  out_request->valid = true;
  DEBUG_TOOL_ASSERT(
    !out_request->storage_buffers.empty(),
    "Preview request must contain at least one storage buffer.");
  DEBUG_TOOL_ASSERT(
    !out_request->stage_name.empty(),
    "Preview request must contain a phase name.");
  return true;
}

}  // namespace debug_tool_backend

