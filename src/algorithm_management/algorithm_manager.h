#pragma once

#include <chrono>
#include <cassert>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "algorithm_support/algorithm_support.h"
#include "common_data/kernel_cfg.h"

#define RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE 1
#include "runtime_systems/runtime_systems.h"
#undef RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE

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

void SetAlgorithmRuntimeShutdownHook();
void ClearAlgorithmExecutionCaches();
bool ExecuteCpuAlgorithmObject(
  const ::agent::AlgorithmObject& object,
  const ::agent::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  ::algorithm::AlgorithmContainerSet* container_set,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  ::agent::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message = nullptr);
bool ExecuteGpuAlgorithmObject(
  const ::agent::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  const ::agent::AgentTickContext& context,
  std::string* out_error_message = nullptr);
bool SynchronizeGpuAlgorithmObject(
  const ::agent::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message = nullptr);

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
    const CpuPipelineRegistration& registration,
    std::string* out_error_message = nullptr);

  bool RegisterPipelineRuntime(
    const std::string& pipeline_name,
    const std::string& owner_agent_name,
    const CpuPipelineRuntimeState& runtime_state,
    std::string* out_error_message = nullptr);

  bool EnqueuePipelineStage0Submission(
    const std::string& pipeline_name,
    const std::string& owner_agent_name,
    const std::string& stage0_algorithm_name,
    const std::vector<AlgorithmResourceBinding>& resource_bindings,
    const std::vector<AlgorithmDescriptorValue>& descriptor_values,
    std::string* out_error_message = nullptr);

  bool MountPipelineAlgorithmObjects(
    std::vector<::algorithm_management::AlgorithmObject>* mounted_objects,
    std::vector<::algorithm_management::AgentAlgorithmRuntimeState>* inout_runtime_states,
    std::vector<::algorithm_management::AlgorithmAssemblyState>* inout_assembly_states,
    std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* standard_shared_container_sets,
    const std::string& owner_agent_name,
    const std::string& pipeline_name,
    const std::vector<AlgorithmPipelineStageSubmission>& stage_submissions,
    AlgorithmExecutionPreference execution_preference,
    AlgorithmPipelineTopology topology,
    AlgorithmPipelineSyncMode sync_mode,
    size_t* out_begin_index = nullptr,
    std::string* out_error_message = nullptr);

  bool EnqueueMountedPipelineStage0Submission(
    std::vector<::algorithm_management::AlgorithmObject>* mounted_objects,
    const std::string& pipeline_name,
    const std::string& owner_agent_name,
    const std::vector<AlgorithmResourceBinding>& resource_bindings,
    const std::vector<AlgorithmDescriptorValue>& descriptor_values,
    std::vector<::algorithm_management::AlgorithmAssemblyState>* inout_assembly_states,
    std::string* out_error_message = nullptr);

  bool TickMountedPipeline(
    std::vector<::algorithm_management::AlgorithmObject>* mounted_objects,
    size_t begin_index,
    size_t end_index,
    const std::string& owner_agent_name,
    const ::agent::AgentTickContext& context,
    const std::vector<bool>& allow_tick_mask,
    const std::vector<::algorithm_management::AlgorithmAssemblyState>& assembly_states,
    bool collect_pipeline_timing,
    std::vector<::algorithm_management::AgentAlgorithmRuntimeState>* inout_runtime_states,
    common_data::AlgorithmToAgentSignal* out_pipeline_signal,
    bool* out_pipeline_processing_failed,
    std::string* out_error_message = nullptr);

  bool ReplayMountedPipelineStageBridgeDebug(
    std::vector<::algorithm_management::AlgorithmObject>* mounted_objects,
    size_t index,
    const ::agent::AgentTickContext& context,
    std::vector<::algorithm_management::AgentAlgorithmRuntimeState>* inout_runtime_states,
    std::string* out_error_message = nullptr);

  void UnregisterPipeline(const std::string& pipeline_name, const std::string& owner_agent_name);

  bool TryGetPipelineRegistration(
    const std::string& pipeline_name,
    CpuPipelineRegistration* out_registration) const;

  bool TryGetPipelineRuntime(
    const std::string& pipeline_name,
    const std::string& owner_agent_name,
    CpuPipelineRuntimeState* out_runtime_state) const;

  bool UpdatePipelineRuntime(
    const std::string& pipeline_name,
    const std::string& owner_agent_name,
    const CpuPipelineRuntimeState& runtime_state,
    std::string* out_error_message = nullptr);

  void Clear();

 private:
  AlgorithmScheduler() = default;

  mutable std::mutex mutex_{};
  std::unordered_map<std::string, CpuPipelineRegistration> pipeline_registrations_{};
  // Shared pipeline runtime storage keyed by pipeline name.
  std::unordered_map<std::string, std::unordered_map<std::string, CpuPipelineRuntimeState>> pipeline_runtime_states_{};
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

namespace pipeline_scheduler_detail {

#ifndef NDEBUG
#define ALGORITHM_SCHEDULER_ASSERT(condition, message) assert((condition) && (message))
#else
#define ALGORITHM_SCHEDULER_ASSERT(condition, message) ((void)0)
#endif

using PipelineRuntimeState = CpuPipelineRuntimeState;
using PipelineLaneRuntimeState = CpuPipelineLaneRuntimeState;
using PendingPipelineStage0Submission = CpuPendingPipelineStage0Submission;
using PipelineInterStageBufferRuntimeState = CpuPipelineInterStageBufferRuntimeState;

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
  const std::vector<::algorithm_management::AlgorithmObject>& algorithm_objects,
  const std::vector<size_t>& executable_indices,
  uint64_t current_lane_id,
  size_t valid_lane_count,
  const std::vector<bool>& stage_has_data,
  size_t pending_stage0_submission_count,
  bool stage0_saturated,
  const std::vector<::algorithm_management::AgentAlgorithmRuntimeState>& runtime_states,
  size_t begin_index,
  size_t end_index) {
  uint64_t hash = kFnvOffsetBasis64;
  for (size_t index = begin_index; index < end_index; ++index) {
    const ::algorithm_management::AlgorithmObject& object = algorithm_objects[index];
    hash = HashString(hash, object.algorithm_profile.algorithm_name);
    hash = HashU64(hash, static_cast<uint64_t>(object.pipeline_stage_index));
    hash = HashU64(hash, static_cast<uint64_t>(object.pipeline_stage_count));
    const algorithm::AlgorithmContainerSet* container_set = object.container_set();
    if (container_set) {
      hash = HashContainerSet(hash, *container_set);
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
  const ::agent::AlgorithmObject& object,
  ::agent::AlgorithmPackageDebugState* out_debug_state) {
  if (out_debug_state) {
    *out_debug_state = {};
  }
  if (auto* complex_support = dynamic_cast<::algorithm_management::IComplexAlgorithmPackageSupport*>(object.reflector.get())) {
    if (out_debug_state) {
      complex_support->CollectDebugState(out_debug_state);
    }
  }
}

inline bool CollectReflectionSnapshot(
  const ::agent::AlgorithmObject& object,
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
  const ::agent::AlgorithmObject& object,
  const common_data::AgentToAlgorithmSignal& signal) {
  if (signal.stop_requested || signal.pause_requested) {
    return true;
  }
  if (!signal.needs_intervention) {
    return false;
  }
  return !object.intervention || object.intervention->SupportsIntervention();
}

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

inline bool CopyPipelineCircularLoopback(
  const algorithm::AlgorithmContainerSet& source_container_set,
  uint32_t shared_variable_count,
  uint32_t shared_array_count,
  ::agent::AlgorithmObject* target_stage,
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
    std::memcpy(target_container->bytes.data(), source_container->bytes.data(), source_container->bytes.size());
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
    std::memcpy(target_container->bytes.data(), source_container->bytes.data(), source_container->bytes.size());
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
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
  ::algorithm_management::AlgorithmObject object{};
  std::shared_ptr<algorithm::AlgorithmContainerSet> container_set{};
  std::string error_message;
  bool ok{false};
};

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
  ::algorithm_management::AlgorithmMountMode mount_mode,
  ::algorithm_management::AlgorithmExecutionPreference execution_preference,
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

  ::algorithm_management::AlgorithmObject object{};
  std::string create_error_message;
  if (!CreateAlgorithmObjectFromLocation(package_location, &object, &create_error_message)) {
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
    mount_mode == ::algorithm_management::AlgorithmMountMode::StandardContainer &&
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
        *out_error_message = "Pipeline stage runtime transfer map topology does not match.";
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
  const std::vector<::algorithm_management::AlgorithmObject>& mounted_objects,
  const std::string& pipeline_name) {
  if (pipeline_name.empty()) {
    return false;
  }
  for (const ::algorithm_management::AlgorithmObject& object : mounted_objects) {
    if (object.pipeline_stage && object.pipeline_name == pipeline_name) {
      return true;
    }
  }
  return false;
}

inline bool FindMountedPipelineRange(
  const std::vector<::algorithm_management::AlgorithmObject>& mounted_objects,
  const std::string& pipeline_name,
  size_t* out_begin_index,
  size_t* out_end_index) {
  if (pipeline_name.empty()) {
    return false;
  }

  for (size_t index = 0u; index < mounted_objects.size(); ++index) {
    const ::algorithm_management::AlgorithmObject& anchor = mounted_objects[index];
    if (!anchor.pipeline_stage || anchor.pipeline_name != pipeline_name) {
      continue;
    }

    size_t begin_index = index;
    while (begin_index > 0u) {
      const ::algorithm_management::AlgorithmObject& previous = mounted_objects[begin_index - 1u];
      if (!previous.pipeline_stage ||
          previous.pipeline_name != pipeline_name ||
          previous.pipeline_stage_index + 1u != mounted_objects[begin_index].pipeline_stage_index) {
        break;
      }
      --begin_index;
    }

    size_t end_index = index + 1u;
    while (end_index < mounted_objects.size()) {
      const ::algorithm_management::AlgorithmObject& next = mounted_objects[end_index];
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
  const ::agent::AlgorithmObject& stage0_object,
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

inline bool TryBuildInitialPipelineLaneRuntimeState(
  const ::agent::AlgorithmObject& stage0_object,
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
  const ::agent::AlgorithmObject& object,
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

inline bool TickAlgorithmObject(
  ::agent::AlgorithmObject& object,
  const ::agent::AgentTickContext& context,
  bool allow_tick,
  bool is_ready,
  bool execute_now,
  AgentAlgorithmRuntimeState* runtime_state) {
  if (!runtime_state) {
    return false;
  }
  runtime_state->algorithm_to_agent_signal = {};
  runtime_state->debug_state = {};
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
  std::string submit_error_message;
  if (!AlgorithmScheduler::Instance().SubmitAlgorithmObject(
        object,
        context,
        runtime_state->agent_to_algorithm_signal,
        object.mutable_container_set(),
        &runtime_state->algorithm_to_agent_signal,
        &runtime_state->debug_state,
        &submit_error_message)) {
    const std::string failure_message = submit_error_message.empty()
      ? std::string("Algorithm execution failed.")
      : std::move(submit_error_message);
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
  if (has_runtime_reflector) {
    const bool should_collect_reflection =
      !capture_reflection_once || !runtime_state->reflection_snapshot_cached;
    if (should_collect_reflection) {
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
    }
  } else if (runtime_state->agent_to_algorithm_signal.reflection_collection_requested) {
    ALGORITHM_SCHEDULER_ASSERT(false, "Reflection was requested but the algorithm has no runtime reflector.");
    runtime_state->algorithm_to_agent_signal.stop_requested = true;
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

inline bool BuildPipelineStageContainerSets(
  const std::vector<::algorithm_management::AlgorithmObject>& algorithm_objects,
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
    const ::algorithm_management::AlgorithmObject& object = algorithm_objects[index];
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

inline void ClearAlgorithmScheduler() {
  AlgorithmScheduler::Instance().Clear();
}

inline bool MountPipelineAlgorithmObjects(
  std::vector<::algorithm_management::AlgorithmObject>* mounted_objects,
  std::vector<::algorithm_management::AgentAlgorithmRuntimeState>* inout_runtime_states,
  std::vector<::algorithm_management::AlgorithmAssemblyState>* inout_assembly_states,
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* standard_shared_container_sets,
  const std::string& owner_agent_name,
  const std::string& pipeline_name,
  const std::vector<AlgorithmPipelineStageSubmission>& stage_submissions,
  AlgorithmExecutionPreference execution_preference,
  AlgorithmPipelineTopology topology,
  AlgorithmPipelineSyncMode sync_mode,
  size_t* out_begin_index = nullptr,
  std::string* out_error_message = nullptr) {
  return AlgorithmScheduler::Instance().MountPipelineAlgorithmObjects(
    mounted_objects,
    inout_runtime_states,
    inout_assembly_states,
    standard_shared_container_sets,
    owner_agent_name,
    pipeline_name,
    stage_submissions,
    execution_preference,
    topology,
    sync_mode,
    out_begin_index,
    out_error_message);
}

inline bool EnqueueMountedPipelineStage0Submission(
  std::vector<::algorithm_management::AlgorithmObject>* mounted_objects,
  const std::string& pipeline_name,
  const std::string& owner_agent_name,
  const std::vector<AlgorithmResourceBinding>& resource_bindings,
  const std::vector<AlgorithmDescriptorValue>& descriptor_values,
  std::vector<::algorithm_management::AlgorithmAssemblyState>* inout_assembly_states,
  std::string* out_error_message = nullptr) {
  return AlgorithmScheduler::Instance().EnqueueMountedPipelineStage0Submission(
    mounted_objects,
    pipeline_name,
    owner_agent_name,
    resource_bindings,
    descriptor_values,
    inout_assembly_states,
    out_error_message);
}

inline bool TickMountedPipeline(
  std::vector<::algorithm_management::AlgorithmObject>* mounted_objects,
  size_t begin_index,
  size_t end_index,
  const std::string& owner_agent_name,
  const ::agent::AgentTickContext& context,
  const std::vector<bool>& allow_tick_mask,
  const std::vector<::algorithm_management::AlgorithmAssemblyState>& assembly_states,
  bool collect_pipeline_timing,
  std::vector<::algorithm_management::AgentAlgorithmRuntimeState>* inout_runtime_states,
  common_data::AlgorithmToAgentSignal* out_pipeline_signal,
  bool* out_pipeline_processing_failed,
  std::string* out_error_message = nullptr) {
  return AlgorithmScheduler::Instance().TickMountedPipeline(
    mounted_objects,
    begin_index,
    end_index,
    owner_agent_name,
    context,
    allow_tick_mask,
    assembly_states,
    collect_pipeline_timing,
    inout_runtime_states,
    out_pipeline_signal,
    out_pipeline_processing_failed,
    out_error_message);
}

inline bool ReplayMountedPipelineStageBridgeDebug(
  std::vector<::algorithm_management::AlgorithmObject>* mounted_objects,
  size_t index,
  const ::agent::AgentTickContext& context,
  std::vector<::algorithm_management::AgentAlgorithmRuntimeState>* inout_runtime_states,
  std::string* out_error_message = nullptr) {
  return AlgorithmScheduler::Instance().ReplayMountedPipelineStageBridgeDebug(
    mounted_objects,
    index,
    context,
    inout_runtime_states,
    out_error_message);
}

inline void UnregisterMountedPipeline(
  const std::string& pipeline_name,
  const std::string& owner_agent_name) {
  AlgorithmScheduler::Instance().UnregisterPipeline(pipeline_name, owner_agent_name);
}

inline bool TryGetMountedPipelineRuntime(
  const std::string& pipeline_name,
  const std::string& owner_agent_name,
  CpuPipelineRuntimeState* out_runtime_state) {
  return AlgorithmScheduler::Instance().TryGetPipelineRuntime(
    pipeline_name,
    owner_agent_name,
    out_runtime_state);
}

inline AlgorithmScheduler& AlgorithmScheduler::Instance() {
  static AlgorithmScheduler instance{};
  static const bool hook_registered = []() {
    SetAlgorithmRuntimeShutdownHook();
    return true;
  }();
  (void)hook_registered;
  return instance;
}

inline void AlgorithmScheduler::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  pipeline_registrations_.clear();
  pipeline_runtime_states_.clear();
  pipeline_runtime_ref_counts_.clear();
  ClearAlgorithmExecutionCaches();
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

    if (!ExecuteGpuAlgorithmObject(
          object,
          container_set,
          context,
          out_error_message)) {
      return false;
    }

    if (object.algorithm_reflector && !object.algorithm_reflector->empty()) {
      if (!SynchronizeGpuAlgorithmObject(
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

  const bool ok = ExecuteCpuAlgorithmObject(
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
  const CpuPipelineRegistration& registration,
  std::string* out_error_message) {
  if (registration.pipeline_name.empty()) {
    if (out_error_message) {
      *out_error_message = "CPU pipeline registration requires a pipeline name.";
    }
    return false;
  }
  if (registration.root_stage_name.empty()) {
    if (out_error_message) {
      *out_error_message = "CPU pipeline registration requires a root stage name.";
    }
    return false;
  }
  if (registration.stage_count == 0u) {
    if (out_error_message) {
      *out_error_message = "CPU pipeline registration requires at least one stage.";
    }
    return false;
  }
  if (registration.mandatory_stage_buffer_slot_name.empty()) {
    if (out_error_message) {
      *out_error_message = "CPU pipeline registration requires the mandatory stage buffer slot name.";
    }
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = pipeline_registrations_.find(registration.pipeline_name);
  if (found != pipeline_registrations_.end()) {
    if (found->second.root_stage_name != registration.root_stage_name ||
        found->second.stage_count != registration.stage_count ||
        found->second.topology != registration.topology ||
        found->second.sync_mode != registration.sync_mode ||
        found->second.max_concurrent_stage0_submissions != registration.max_concurrent_stage0_submissions ||
        found->second.mandatory_stage_buffer_slot_name != registration.mandatory_stage_buffer_slot_name) {
      if (out_error_message) {
        *out_error_message =
          "CPU pipeline registration conflicts with an existing mounted pipeline: " + registration.pipeline_name;
      }
      return false;
    }
  } else {
    pipeline_registrations_.emplace(registration.pipeline_name, registration);
  }
  pipeline_runtime_states_.erase(registration.pipeline_name);
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

inline bool AlgorithmScheduler::RegisterPipelineRuntime(
  const std::string& pipeline_name,
  const std::string& owner_agent_name,
  const CpuPipelineRuntimeState& runtime_state,
  std::string* out_error_message) {
  if (pipeline_name.empty()) {
    if (out_error_message) {
      *out_error_message = "CPU pipeline runtime registration requires a pipeline name.";
    }
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  const auto registration_it = pipeline_registrations_.find(pipeline_name);
  if (registration_it != pipeline_registrations_.end()) {
    const CpuPipelineRegistration& registration = registration_it->second;
    if (runtime_state.mandatory_stage_buffer_slot_name != registration.mandatory_stage_buffer_slot_name) {
      if (out_error_message) {
        *out_error_message = "CPU pipeline runtime registration stage buffer slot does not match the registration.";
      }
      return false;
    }
  }
  pipeline_runtime_states_[pipeline_name][owner_agent_name] = runtime_state;
  ++pipeline_runtime_ref_counts_[pipeline_name][owner_agent_name];
  if (out_error_message) {
    out_error_message->clear();
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
  CpuPipelineRegistration registration{};
  CpuPipelineRuntimeState pipeline_runtime_state{};
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
  for (const CpuPipelineLaneRuntimeState& lane_state : pipeline_runtime_state.lanes) {
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

  CpuPendingPipelineStage0Submission submission{};
  submission.owner_agent_name = owner_agent_name;
  submission.lane_id = pipeline_runtime_state.next_lane_id;
  submission.loop_lane_active = false;
  submission.prepared_container_set = prepared_stage0_object.shared_container_set;
  submission.resource_bindings = resource_bindings;
  submission.descriptor_values = descriptor_values;

  CpuPipelineLaneRuntimeState queued_lane_state{};
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

inline bool AlgorithmScheduler::MountPipelineAlgorithmObjects(
  std::vector<::algorithm_management::AlgorithmObject>* mounted_objects,
  std::vector<::algorithm_management::AgentAlgorithmRuntimeState>* inout_runtime_states,
  std::vector<::algorithm_management::AlgorithmAssemblyState>* inout_assembly_states,
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* standard_shared_container_sets,
  const std::string& owner_agent_name,
  const std::string& pipeline_name,
  const std::vector<AlgorithmPipelineStageSubmission>& stage_submissions,
  AlgorithmExecutionPreference execution_preference,
  AlgorithmPipelineTopology topology,
  AlgorithmPipelineSyncMode sync_mode,
  size_t* out_begin_index,
  std::string* out_error_message) {
  auto set_error = [&](std::string message) {
    if (out_error_message) {
      *out_error_message = std::move(message);
    }
  };

  if (!mounted_objects || !inout_runtime_states || !inout_assembly_states) {
    set_error("Pipeline mount received a null output pointer.");
    return false;
  }
  if (owner_agent_name.empty()) {
    set_error("Owner agent name must not be empty.");
    return false;
  }
  if (pipeline_name.empty()) {
    set_error("Pipeline name must not be empty.");
    return false;
  }
  if (stage_submissions.empty()) {
    set_error("Pipeline stage submission list must not be empty.");
    return false;
  }
  if (pipeline_scheduler_detail::PipelineNameInUse(*mounted_objects, pipeline_name)) {
    set_error("Pipeline name is already in use.");
    return false;
  }

  std::vector<pipeline_scheduler_detail::BuiltAlgorithmMount> built_stages{};
  built_stages.reserve(stage_submissions.size());
  std::unordered_set<std::string> seen_stage_names{};
  bool pipeline_supports_circular_submission = false;
  for (size_t stage_index = 0u; stage_index < stage_submissions.size(); ++stage_index) {
    const AlgorithmPipelineStageSubmission& stage_submission = stage_submissions[stage_index];
    if (stage_submission.stage_name.empty()) {
      set_error("Pipeline stage name must not be empty.");
      return false;
    }
    if (!seen_stage_names.insert(stage_submission.stage_name).second) {
      set_error("Pipeline stage name is duplicated inside the submission: " + stage_submission.stage_name);
      return false;
    }

    pipeline_scheduler_detail::BuiltAlgorithmMount built_mount =
      pipeline_scheduler_detail::BuildAlgorithmMount(
        stage_submission.stage_name,
        stage_submission.resource_bindings,
        stage_submission.descriptor_values,
        ::algorithm_management::AlgorithmMountMode::Pipeline,
        execution_preference,
        standard_shared_container_sets);
    if (!built_mount.ok) {
      set_error(built_mount.error_message);
      return false;
    }
    if (!built_mount.container_set || !built_mount.container_set->standard_layout.enabled()) {
      set_error("Pipeline stage standard container layout is unavailable: " + stage_submission.stage_name);
      return false;
    }
    if (!algorithm::HasMandatoryPipelineStageBuffer(*built_mount.container_set)) {
      set_error(
        "Pipeline stage must expose the mandatory implicit stage buffer in its standard container: " +
        stage_submission.stage_name);
      return false;
    }

    built_mount.object.mount_mode = ::algorithm_management::AlgorithmMountMode::Pipeline;
    built_mount.object.pipeline_stage = true;
    built_mount.object.pipeline_name = pipeline_name;
    built_mount.object.pipeline_stage_index = static_cast<uint32_t>(stage_index);
    built_mount.object.pipeline_stage_count = static_cast<uint32_t>(stage_submissions.size());
    built_mount.object.pipeline_topology = topology;
    built_mount.object.pipeline_sync_mode = sync_mode;
    if (stage_index == 0u &&
        built_mount.object.runtime_transfer_map &&
        built_mount.object.runtime_transfer_map->SupportsCircularTick()) {
      pipeline_supports_circular_submission = true;
    }
    built_stages.push_back(std::move(built_mount));
  }

  std::shared_ptr<algorithm::AlgorithmRuntimeTransferMap> shared_pipeline_transfer_map{};
  std::string transfer_map_error_message;
  if (!pipeline_scheduler_detail::TryBuildMountedPipelineTransferMap(
        built_stages,
        &shared_pipeline_transfer_map,
        &transfer_map_error_message)) {
    set_error(transfer_map_error_message.empty()
      ? "Failed to build the mounted pipeline runtime transfer map."
      : std::move(transfer_map_error_message));
    return false;
  }
  ALGORITHM_SCHEDULER_ASSERT(
    shared_pipeline_transfer_map && shared_pipeline_transfer_map->valid,
    "Mounted pipeline runtime transfer map must be valid.");
  for (pipeline_scheduler_detail::BuiltAlgorithmMount& built_stage : built_stages) {
    built_stage.object.runtime_transfer_map = shared_pipeline_transfer_map;
  }

  if (topology == ::algorithm_management::AlgorithmPipelineTopology::Circular &&
      !pipeline_supports_circular_submission) {
    set_error("This pipeline algorithm does not support circular pipeline submission.");
    return false;
  }

  const size_t pipeline_begin_index = mounted_objects->size();
  const size_t previous_runtime_state_count = inout_runtime_states->size();
  const size_t previous_assembly_state_count = inout_assembly_states->size();
  for (size_t stage_index = 0u; stage_index < built_stages.size(); ++stage_index) {
    mounted_objects->push_back(std::move(built_stages[stage_index].object));
    inout_runtime_states->push_back(::algorithm_management::AgentAlgorithmRuntimeState{
      .algorithm_name = mounted_objects->back().algorithm_profile.algorithm_name,
    });
    inout_assembly_states->push_back(::algorithm_management::AlgorithmAssemblyState::Pending);
  }

  const auto rollback_mount_vectors = [&]() {
    mounted_objects->resize(pipeline_begin_index);
    inout_runtime_states->resize(previous_runtime_state_count);
    inout_assembly_states->resize(previous_assembly_state_count);
  };

  for (size_t i = 0u; i < stage_submissions.size(); ++i) {
    const size_t stage_index = pipeline_begin_index + i;
    (*mounted_objects)[stage_index].mount_mode = ::algorithm_management::AlgorithmMountMode::Pipeline;
    (*mounted_objects)[stage_index].pipeline_stage = true;
    (*mounted_objects)[stage_index].pipeline_name = pipeline_name;
    (*mounted_objects)[stage_index].pipeline_stage_index = static_cast<uint32_t>(i);
    (*mounted_objects)[stage_index].pipeline_stage_count = static_cast<uint32_t>(stage_submissions.size());
    (*mounted_objects)[stage_index].pipeline_topology = topology;
    (*mounted_objects)[stage_index].pipeline_sync_mode = sync_mode;
    (*inout_assembly_states)[stage_index] = ::algorithm_management::AlgorithmAssemblyState::Ready;
  }

  ::algorithm_management::CpuPipelineRuntimeState pipeline_runtime_state{};
  pipeline_runtime_state.owner_agent_name = owner_agent_name;
  pipeline_runtime_state.topology = topology;
  pipeline_runtime_state.sync_mode = sync_mode;
  pipeline_runtime_state.max_concurrent_stage0_submissions =
    common_data::DefaultPipelineMaxConcurrentStage0Submissions();
  pipeline_runtime_state.mandatory_stage_buffer_slot_name =
    shared_pipeline_transfer_map->pipeline_shared_stage_buffer_slot_name;
  pipeline_runtime_state.stage_has_data.assign(stage_submissions.size(), false);
  if (topology == ::algorithm_management::AlgorithmPipelineTopology::NonCircular &&
      !pipeline_runtime_state.stage_has_data.empty()) {
    pipeline_runtime_state.stage_has_data.front() = true;
  }

  pipeline_scheduler_detail::PipelineLaneRuntimeState initial_lane_state{};
  std::string lane_error_message;
  if (!pipeline_scheduler_detail::TryBuildInitialPipelineLaneRuntimeState(
        (*mounted_objects)[pipeline_begin_index],
        stage_submissions.size(),
        owner_agent_name,
        topology == ::algorithm_management::AlgorithmPipelineTopology::Circular,
        pipeline_runtime_state.next_lane_id,
        pipeline_runtime_state.mandatory_stage_buffer_slot_name,
        &initial_lane_state,
        &lane_error_message)) {
    rollback_mount_vectors();
    set_error(lane_error_message.empty()
      ? "Failed to initialize pipeline lane runtime state."
      : std::move(lane_error_message));
    return false;
  }
  if (topology == ::algorithm_management::AlgorithmPipelineTopology::NonCircular &&
      !initial_lane_state.stage_has_data.empty()) {
    initial_lane_state.stage_has_data.front() = true;
  }
  pipeline_runtime_state.current_lane_id = initial_lane_state.lane_id;
  pipeline_runtime_state.lanes.push_back(std::move(initial_lane_state));
  ++pipeline_runtime_state.next_lane_id;

  const bool registered = RegisterPipeline(
    ::algorithm_management::CpuPipelineRegistration{
      .pipeline_name = pipeline_name,
      .root_stage_name = (*mounted_objects)[pipeline_begin_index].algorithm_profile.algorithm_name,
      .stage_count = static_cast<uint32_t>(stage_submissions.size()),
      .topology = topology,
      .sync_mode = sync_mode,
      .max_concurrent_stage0_submissions =
        common_data::DefaultPipelineMaxConcurrentStage0Submissions(),
      .mandatory_stage_buffer_slot_name =
        shared_pipeline_transfer_map->pipeline_shared_stage_buffer_slot_name,
    },
    out_error_message);
  if (!registered) {
    rollback_mount_vectors();
    return false;
  }

  const bool runtime_registered = RegisterPipelineRuntime(
    pipeline_name,
    owner_agent_name,
    pipeline_runtime_state,
    out_error_message);
  if (!runtime_registered) {
    rollback_mount_vectors();
    return false;
  }

  if (out_begin_index) {
    *out_begin_index = pipeline_begin_index;
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

inline bool AlgorithmScheduler::EnqueueMountedPipelineStage0Submission(
  std::vector<::algorithm_management::AlgorithmObject>* mounted_objects,
  const std::string& pipeline_name,
  const std::string& owner_agent_name,
  const std::vector<AlgorithmResourceBinding>& resource_bindings,
  const std::vector<AlgorithmDescriptorValue>& descriptor_values,
  std::vector<::algorithm_management::AlgorithmAssemblyState>* inout_assembly_states,
  std::string* out_error_message) {
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

  std::vector<::algorithm_management::AlgorithmObject>& algorithm_objects = *mounted_objects;
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

  ::algorithm_management::AlgorithmObject& stage0_object = algorithm_objects[pipeline_begin_index];
  if (stage0_object.execution_preference == ::algorithm_management::AlgorithmExecutionPreference::Cpu) {
    if (!EnqueuePipelineStage0Submission(
          pipeline_name,
          owner_agent_name,
          stage0_object.algorithm_profile.algorithm_name,
          resource_bindings,
          descriptor_values,
          out_error_message)) {
      return false;
    }
    if (inout_assembly_states->size() < algorithm_objects.size()) {
      inout_assembly_states->resize(
        algorithm_objects.size(),
        ::algorithm_management::AlgorithmAssemblyState::Pending);
    }
    for (size_t stage_index = pipeline_begin_index; stage_index < pipeline_end_index; ++stage_index) {
      (*inout_assembly_states)[stage_index] = ::algorithm_management::AlgorithmAssemblyState::Ready;
    }
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  CpuPipelineRuntimeState pipeline_state{};
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

  ::agent::AlgorithmObject prepared_stage0_object{};
  std::string prepare_error_message;
  if (!PrepareAlgorithmObjectByName(
        stage0_object.algorithm_profile.algorithm_name,
        resource_bindings,
        descriptor_values,
        &prepared_stage0_object,
        &prepare_error_message)) {
    set_error(prepare_error_message.empty()
      ? "Failed to prepare the pipeline stage0 submission."
      : std::move(prepare_error_message));
    return false;
  }

  CpuPendingPipelineStage0Submission submission{};
  submission.owner_agent_name = owner_agent_name;
  submission.lane_id = pipeline_state.next_lane_id;
  submission.loop_lane_active = false;
  submission.prepared_container_set = prepared_stage0_object.shared_container_set;
  submission.resource_bindings = resource_bindings;
  submission.descriptor_values = descriptor_values;

  CpuPipelineLaneRuntimeState queued_lane_state{};
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
      ::algorithm_management::AlgorithmAssemblyState::Pending);
  }
  for (size_t stage_index = pipeline_begin_index; stage_index < pipeline_end_index; ++stage_index) {
    (*inout_assembly_states)[stage_index] = ::algorithm_management::AlgorithmAssemblyState::Ready;
  }

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
  bool collect_pipeline_timing,
  std::vector<::algorithm_management::AgentAlgorithmRuntimeState>* inout_runtime_states,
  common_data::AlgorithmToAgentSignal* out_pipeline_signal,
  bool* out_pipeline_processing_failed,
  std::string* out_error_message) {
  auto set_error = [&](std::string message) {
    if (out_error_message) {
      *out_error_message = std::move(message);
    }
  };

  if (!mounted_objects || !inout_runtime_states || !out_pipeline_signal || !out_pipeline_processing_failed) {
    set_error("Mounted pipeline tick received a null output pointer.");
    return false;
  }
  if (begin_index >= end_index || end_index > mounted_objects->size()) {
    set_error("Mounted pipeline tick received an invalid pipeline range.");
    return false;
  }

  std::vector<::algorithm_management::AlgorithmObject>& algorithm_objects = *mounted_objects;
  std::vector<::algorithm_management::AgentAlgorithmRuntimeState>& updated_runtime_states =
    *inout_runtime_states;
  updated_runtime_states.resize(algorithm_objects.size());
  *out_pipeline_signal = {};
  *out_pipeline_processing_failed = false;

  ::algorithm_management::AlgorithmObject& root_object = algorithm_objects[begin_index];
  const bool pipeline_uses_inter_stage_buffer =
    root_object.execution_preference == ::algorithm_management::AlgorithmExecutionPreference::Cpu;

  CpuPipelineRegistration registration{};
  CpuPipelineRuntimeState pipeline_state{};
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto registration_it = pipeline_registrations_.find(root_object.pipeline_name);
    if (registration_it == pipeline_registrations_.end()) {
      set_error("Mounted pipeline registration is unavailable.");
      return false;
    }
    registration = registration_it->second;
    const auto runtime_it = pipeline_runtime_states_.find(root_object.pipeline_name);
    if (runtime_it == pipeline_runtime_states_.end()) {
      set_error("Mounted pipeline runtime state is unavailable.");
      return false;
    }
    const auto owner_runtime_it = runtime_it->second.find(owner_agent_name);
    if (owner_runtime_it == runtime_it->second.end()) {
      set_error("Mounted pipeline runtime state is unavailable for the selected agent.");
      return false;
    }
    pipeline_state = owner_runtime_it->second;
  }

  const bool pipeline_stage_debug_all = root_object.pipeline_stage_debug_all;
  const uint32_t pipeline_stage_debug_index = root_object.pipeline_stage_debug_index;
  const size_t pipeline_stage_count = end_index - begin_index;

  if (pipeline_state.lanes.empty()) {
    pipeline_scheduler_detail::PipelineLaneRuntimeState fallback_lane_state{};
    std::string fallback_lane_error_message;
    if (!pipeline_scheduler_detail::TryBuildInitialPipelineLaneRuntimeState(
          algorithm_objects[begin_index],
          pipeline_stage_count,
          owner_agent_name,
          pipeline_state.topology == ::algorithm_management::AlgorithmPipelineTopology::Circular,
          pipeline_state.next_lane_id,
          pipeline_state.mandatory_stage_buffer_slot_name,
          &fallback_lane_state,
          &fallback_lane_error_message)) {
      const std::string failure_message = fallback_lane_error_message.empty()
        ? std::string("Failed to rebuild the primary pipeline lane runtime state.")
        : std::move(fallback_lane_error_message);
      ALGORITHM_SCHEDULER_ASSERT(false, failure_message.c_str());
      set_error(failure_message);
      out_pipeline_signal->stop_requested = true;
      *out_pipeline_processing_failed = true;
      return false;
    }
    pipeline_state.current_lane_id = fallback_lane_state.lane_id;
    pipeline_state.lanes.push_back(std::move(fallback_lane_state));
    ++pipeline_state.next_lane_id;
  }

  pipeline_scheduler_detail::SyncPipelineLegacyStageStateFromPrimaryLane(&pipeline_state, pipeline_stage_count);
  if (pipeline_state.stage_has_data.size() != pipeline_stage_count) {
    pipeline_state.stage_has_data.assign(pipeline_stage_count, false);
  }
  pipeline_scheduler_detail::PipelineLaneRuntimeState* primary_lane_state =
    pipeline_scheduler_detail::FindPrimaryPipelineLaneRuntimeState(&pipeline_state);
  ALGORITHM_SCHEDULER_ASSERT(
    primary_lane_state != nullptr,
    "Primary pipeline lane runtime state is unavailable.");
  if (!primary_lane_state) {
    set_error("Primary pipeline lane runtime state is unavailable.");
    out_pipeline_signal->stop_requested = true;
    *out_pipeline_processing_failed = true;
    return false;
  }

  pipeline_scheduler_detail::PipelineGroupProgressState previous_progress_state{};
  if (begin_index < updated_runtime_states.size()) {
    previous_progress_state.signature = updated_runtime_states[begin_index].pipeline_progress_signature;
    previous_progress_state.signature_valid =
      updated_runtime_states[begin_index].pipeline_progress_signature_valid;
    previous_progress_state.no_progress_seconds =
      updated_runtime_states[begin_index].pipeline_no_progress_seconds;
    previous_progress_state.stall_report_requested =
      updated_runtime_states[begin_index].pipeline_stall_report_requested;
    previous_progress_state.stall_reported =
      updated_runtime_states[begin_index].pipeline_stall_reported;
    previous_progress_state.stall_reason =
      updated_runtime_states[begin_index].pipeline_stall_reason;
    if (collect_pipeline_timing) {
      previous_progress_state.stage_runtime_stats =
        updated_runtime_states[begin_index].pipeline_stage_runtime_stats;
    }
  }

  pipeline_scheduler_detail::PipelineGroupProgressState progress_state = previous_progress_state;
  progress_state.stall_reason.clear();
  std::vector<::algorithm_management::AlgorithmPipelineStageRuntimeStat>* pipeline_stage_runtime_stats =
    collect_pipeline_timing ? &progress_state.stage_runtime_stats : nullptr;
  if (collect_pipeline_timing) {
    progress_state.stage_runtime_stats.clear();
    progress_state.stage_runtime_stats.reserve(pipeline_stage_count);
  } else {
    progress_state.stage_runtime_stats.clear();
  }

  algorithm_objects[begin_index].SetContainerSet(primary_lane_state->standard_container_set);
  algorithm_objects[begin_index].resource_bindings = primary_lane_state->resource_bindings;
  algorithm_objects[begin_index].descriptor_values = primary_lane_state->descriptor_values;

  std::vector<bool> current_stage_has_data = primary_lane_state->stage_has_data;
  std::vector<bool> next_stage_has_data(pipeline_stage_count, false);
  auto resolve_pipeline_active_stage = [&](
    const std::vector<bool>& stage_has_data,
    const std::vector<size_t>& executable_stage_indices,
    size_t* out_active_stage_index,
    bool* out_active_stage_valid) {
    *out_active_stage_index = 0u;
    *out_active_stage_valid = false;
    for (size_t stage_offset = stage_has_data.size(); stage_offset > 0u; --stage_offset) {
      if (stage_has_data[stage_offset - 1u]) {
        *out_active_stage_index = stage_offset - 1u;
        *out_active_stage_valid = true;
        return;
      }
    }
    if (!executable_stage_indices.empty()) {
      *out_active_stage_index = executable_stage_indices.back() - begin_index;
      *out_active_stage_valid = true;
    }
  };

  const bool stage0_allow_tick = begin_index < allow_tick_mask.size() ? allow_tick_mask[begin_index] : true;
  const bool stage0_is_ready =
    begin_index < assembly_states.size() &&
    assembly_states[begin_index] == ::algorithm_management::AlgorithmAssemblyState::Ready;
  if (!current_stage_has_data.empty() &&
      !current_stage_has_data.front() &&
      stage0_allow_tick &&
      stage0_is_ready &&
      !pipeline_state.pending_stage0_submissions.empty() &&
      (pipeline_stage_debug_all || pipeline_stage_debug_index == 0u)) {
    ::algorithm_management::AlgorithmObject& stage0_object = algorithm_objects[begin_index];
    CpuPendingPipelineStage0Submission submission =
      std::move(pipeline_state.pending_stage0_submissions.front());
    pipeline_state.pending_stage0_submissions.erase(pipeline_state.pending_stage0_submissions.begin());
    if (pipeline_scheduler_detail::CountLivePipelineStages(current_stage_has_data) == 0u &&
        pipeline_state.current_lane_id != 0u &&
        pipeline_state.current_lane_id != submission.lane_id) {
      if (pipeline_scheduler_detail::PipelineLaneRuntimeState* previous_lane_state =
            pipeline_scheduler_detail::FindPipelineLaneRuntimeStateById(
              &pipeline_state,
              pipeline_state.current_lane_id)) {
        previous_lane_state->valid = false;
      }
    }
    submission.owner_agent_name = owner_agent_name;
    pipeline_state.current_lane_id = submission.lane_id;
    primary_lane_state = pipeline_scheduler_detail::FindPrimaryPipelineLaneRuntimeState(&pipeline_state);
    ALGORITHM_SCHEDULER_ASSERT(
      primary_lane_state != nullptr,
      "Submitted pipeline lane runtime state is unavailable.");
    if (!primary_lane_state) {
      set_error("Submitted pipeline lane runtime state is unavailable.");
      out_pipeline_signal->stop_requested = true;
      *out_pipeline_processing_failed = true;
      return false;
    }
    stage0_object.resource_bindings = submission.resource_bindings;
    stage0_object.descriptor_values = submission.descriptor_values;
    stage0_object.SetContainerSet(primary_lane_state->standard_container_set);
    primary_lane_state->owner_agent_name = owner_agent_name;
    primary_lane_state->lane_id = submission.lane_id;
    primary_lane_state->loop_lane_active = submission.loop_lane_active;
    primary_lane_state->standard_container_set = stage0_object.shared_container_set;
    primary_lane_state->resource_bindings = stage0_object.resource_bindings;
    primary_lane_state->descriptor_values = stage0_object.descriptor_values;
    primary_lane_state->stage_has_data.assign(pipeline_stage_count, false);
    current_stage_has_data.front() = true;
    pipeline_state.stage0_saturated = false;
  }

  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>> stage_container_sets{};
  std::string stage_set_error_message;
  if (!pipeline_scheduler_detail::BuildPipelineStageContainerSets(
        algorithm_objects,
        begin_index,
        end_index,
        &stage_container_sets,
        &stage_set_error_message)) {
    const std::string failure_message = stage_set_error_message.empty()
      ? std::string("Failed to build pipeline stage container sets.")
      : std::move(stage_set_error_message);
    ALGORITHM_SCHEDULER_ASSERT(false, failure_message.c_str());
    set_error(failure_message);
    out_pipeline_signal->stop_requested = true;
    *out_pipeline_processing_failed = true;
    return false;
  }

  std::vector<size_t> executable_indices{};
  executable_indices.reserve(pipeline_stage_count);
  std::vector<bool> stage_allow_tick(pipeline_stage_count, false);
  std::vector<bool> stage_is_ready(pipeline_stage_count, false);
  std::vector<bool> stage_launch_once_completed(pipeline_stage_count, false);
  std::vector<bool> stage_execute_mask(pipeline_stage_count, false);

  for (size_t index = begin_index; index < end_index; ++index) {
    const size_t stage_offset = index - begin_index;
    ::algorithm_management::AlgorithmObject& object = algorithm_objects[index];
    ::algorithm_management::AgentAlgorithmRuntimeState runtime_state{};
    if (index < updated_runtime_states.size()) {
      runtime_state = updated_runtime_states[index];
    } else {
      runtime_state.algorithm_name = object.algorithm_profile.algorithm_name;
    }
    pipeline_scheduler_detail::ResetRuntimeStateBase(runtime_state, &runtime_state);
    updated_runtime_states[index] = std::move(runtime_state);

    if (collect_pipeline_timing) {
      progress_state.stage_runtime_stats.push_back(::algorithm_management::AlgorithmPipelineStageRuntimeStat{
        .stage_name = object.algorithm_profile.algorithm_name,
        .elapsed_seconds = 0.0f,
        .reason = {},
      });
    }

    stage_allow_tick[stage_offset] = index < allow_tick_mask.size() ? allow_tick_mask[index] : true;
    stage_is_ready[stage_offset] =
      index < assembly_states.size() &&
      assembly_states[index] == ::algorithm_management::AlgorithmAssemblyState::Ready;
    const bool launch_once_then_hold =
      object.tick_lifetime == ::algorithm_management::AlgorithmTickLifetime::LaunchOnceThenHold;
    stage_launch_once_completed[stage_offset] =
      launch_once_then_hold && updated_runtime_states[index].launch_once_completed;
    const bool has_data = stage_offset < current_stage_has_data.size() && current_stage_has_data[stage_offset];
    stage_execute_mask[stage_offset] =
      has_data &&
      stage_allow_tick[stage_offset] &&
      stage_is_ready[stage_offset] &&
      !stage_launch_once_completed[stage_offset] &&
      (pipeline_stage_debug_all || stage_offset == pipeline_stage_debug_index);
  }

  for (size_t index = begin_index; index < end_index; ++index) {
    const size_t stage_offset = index - begin_index;
    ::algorithm_management::AlgorithmObject& object = algorithm_objects[index];
    ::algorithm_management::AgentAlgorithmRuntimeState& runtime_state = updated_runtime_states[index];
    const bool has_data = stage_offset < current_stage_has_data.size() && current_stage_has_data[stage_offset];
    const bool execute_now = stage_offset < stage_execute_mask.size() && stage_execute_mask[stage_offset];
    if (!execute_now) {
      pipeline_scheduler_detail::SetPipelineStageRuntimeReason(
        pipeline_stage_runtime_stats,
        object.algorithm_profile.algorithm_name,
        !has_data
          ? pipeline_scheduler_detail::BuildPipelineStageNoInputReason(
              stage_offset == 0u,
              !pipeline_state.pending_stage0_submissions.empty())
          : (stage_allow_tick[stage_offset] &&
             stage_is_ready[stage_offset] &&
             !stage_launch_once_completed[stage_offset])
              ? "Stage is holding data, but the downstream stage cannot accept output this tick."
              : pipeline_scheduler_detail::BuildPipelineStageIdleReason(
                  stage_allow_tick[stage_offset],
                  stage_is_ready[stage_offset],
                  stage_launch_once_completed[stage_offset]));
      if (!pipeline_scheduler_detail::TickAlgorithmObject(
            object,
            context,
            stage_allow_tick[stage_offset],
            stage_is_ready[stage_offset],
            false,
            &runtime_state)) {
        runtime_state.algorithm_to_agent_signal.stop_requested = true;
      }
      if (has_data) {
        next_stage_has_data[stage_offset] = true;
      }
      pipeline_scheduler_detail::MergeAlgorithmToAgentSignal(
        runtime_state.algorithm_to_agent_signal,
        out_pipeline_signal);
      continue;
    }
    pipeline_scheduler_detail::SetPipelineStageRuntimeReason(
      pipeline_stage_runtime_stats,
      object.algorithm_profile.algorithm_name,
      "Stage executed.");
    executable_indices.push_back(index);
  }

  auto commit_progress_state = [&](pipeline_scheduler_detail::PipelineGroupProgressState&& applied_progress_state) {
    if (begin_index >= updated_runtime_states.size() || begin_index >= end_index) {
      return;
    }
    size_t active_stage_index = 0u;
    bool active_stage_valid = false;
    resolve_pipeline_active_stage(
      pipeline_state.stage_has_data,
      executable_indices,
      &active_stage_index,
      &active_stage_valid);
    for (size_t index = begin_index; index < end_index; ++index) {
      ::algorithm_management::AgentAlgorithmRuntimeState& runtime_state = updated_runtime_states[index];
      runtime_state.pipeline_progress_signature = applied_progress_state.signature;
      runtime_state.pipeline_progress_signature_valid = applied_progress_state.signature_valid;
      runtime_state.pipeline_no_progress_seconds = applied_progress_state.no_progress_seconds;
      runtime_state.pipeline_stall_report_requested = applied_progress_state.stall_report_requested;
      runtime_state.pipeline_stall_reported = applied_progress_state.stall_reported;
      runtime_state.pipeline_stall_reason = applied_progress_state.stall_reason;
      runtime_state.pipeline_active_stage_index = static_cast<uint32_t>(active_stage_index);
      runtime_state.pipeline_active_stage_index_valid = active_stage_valid;
      if (collect_pipeline_timing) {
        runtime_state.pipeline_total_elapsed_seconds = applied_progress_state.total_elapsed_seconds;
        runtime_state.pipeline_stage_runtime_stats = applied_progress_state.stage_runtime_stats;
      } else {
        runtime_state.pipeline_total_elapsed_seconds = 0.0f;
        runtime_state.pipeline_stage_runtime_stats.clear();
      }
    }
  };

  if (!pipeline_scheduler_detail::PipelineGroupIsExecutable(executable_indices)) {
    pipeline_scheduler_detail::CommitPipelineStageStateToPrimaryLane(&pipeline_state, next_stage_has_data);
    const bool pipeline_has_live_token =
      pipeline_scheduler_detail::CountLivePipelineStages(pipeline_state.stage_has_data) > 0u;
    const uint64_t current_signature =
      pipeline_scheduler_detail::HashPipelineGroupState(
        algorithm_objects,
        executable_indices,
        pipeline_state.current_lane_id,
        pipeline_scheduler_detail::CountValidPipelineLanes(pipeline_state),
        pipeline_state.stage_has_data,
        pipeline_state.pending_stage0_submissions.size(),
        pipeline_state.stage0_saturated,
        updated_runtime_states,
        begin_index,
        end_index);
    if (!pipeline_has_live_token && pipeline_state.pending_stage0_submissions.empty()) {
      progress_state.signature = current_signature;
      progress_state.signature_valid = true;
      progress_state.no_progress_seconds = 0.0f;
      progress_state.stall_report_requested = false;
      progress_state.stall_reported = false;
      progress_state.stall_reason = "Pipeline is idle and waiting for an external resource batch.";
    } else {
      pipeline_scheduler_detail::UpdatePipelineGroupProgressState(
        previous_progress_state,
        current_signature,
        true,
        context.dt_seconds,
        &progress_state);
      progress_state.stall_reason = "No pipeline stage was executable during this tick.";
    }
    commit_progress_state(std::move(progress_state));
    {
      std::lock_guard<std::mutex> lock(mutex_);
      pipeline_runtime_states_[root_object.pipeline_name][owner_agent_name] = std::move(pipeline_state);
    }
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  for (size_t index : executable_indices) {
    ::algorithm_management::AlgorithmObject& object = algorithm_objects[index];
    ::algorithm_management::AgentAlgorithmRuntimeState& runtime_state = updated_runtime_states[index];
    const auto stage_begin = std::chrono::steady_clock::now();
    algorithm_support::PipelineStageBridge bridge(object.runtime_transfer_map);
    std::string ingress_error_message;
    const bool ingress_ok = pipeline_uses_inter_stage_buffer
      ? bridge.IngestFromPreviousStage(
          object.algorithm_profile.algorithm_name,
          stage_container_sets,
          primary_lane_state->inter_stage_buffer,
          object.mutable_container_set(),
          &ingress_error_message)
      : bridge.IngestFromPreviousStage(
          object.algorithm_profile.algorithm_name,
          stage_container_sets,
          object.mutable_container_set(),
          &ingress_error_message);
    if (!ingress_ok) {
      const std::string failure_message = ingress_error_message.empty()
        ? std::string("Failed to ingest pipeline stage input.")
        : std::move(ingress_error_message);
      ALGORITHM_SCHEDULER_ASSERT(false, failure_message.c_str());
      runtime_state.algorithm_to_agent_signal.stop_requested = true;
      pipeline_scheduler_detail::SetPipelineStageRuntimeReason(
        pipeline_stage_runtime_stats,
        object.algorithm_profile.algorithm_name,
        "Ingress failed: " + failure_message);
      pipeline_scheduler_detail::MergeAlgorithmToAgentSignal(
        runtime_state.algorithm_to_agent_signal,
        out_pipeline_signal);
      set_error(failure_message);
      *out_pipeline_processing_failed = true;
      return false;
    }
    std::string ingress_debug_error_message;
    if (!bridge.CaptureIngressDebugSet(
          object.pipeline_name,
          object.algorithm_profile.algorithm_name,
          stage_container_sets,
          *object.container_set(),
          &runtime_state.bridge_debug_set,
          &ingress_debug_error_message)) {
      const std::string failure_message = ingress_debug_error_message.empty()
        ? std::string("Failed to capture pipeline ingress debug state.")
        : std::move(ingress_debug_error_message);
      ALGORITHM_SCHEDULER_ASSERT(false, failure_message.c_str());
      runtime_state.algorithm_to_agent_signal.stop_requested = true;
      pipeline_scheduler_detail::SetPipelineStageRuntimeReason(
        pipeline_stage_runtime_stats,
        object.algorithm_profile.algorithm_name,
        "Ingress debug capture failed: " + failure_message);
      pipeline_scheduler_detail::MergeAlgorithmToAgentSignal(
        runtime_state.algorithm_to_agent_signal,
        out_pipeline_signal);
      set_error(failure_message);
      *out_pipeline_processing_failed = true;
      return false;
    }
    const auto stage_end = std::chrono::steady_clock::now();
    pipeline_scheduler_detail::AddPipelineStageRuntimeElapsed(
      pipeline_stage_runtime_stats,
      object.algorithm_profile.algorithm_name,
      std::chrono::duration<float>(stage_end - stage_begin).count());
  }

  for (size_t index : executable_indices) {
    ::algorithm_management::AlgorithmObject& object = algorithm_objects[index];
    ::algorithm_management::AgentAlgorithmRuntimeState& runtime_state = updated_runtime_states[index];
    const bool allow_tick = index < allow_tick_mask.size() ? allow_tick_mask[index] : true;
    const bool is_ready =
      index < assembly_states.size() &&
      assembly_states[index] == ::algorithm_management::AlgorithmAssemblyState::Ready;
    const auto stage_begin = std::chrono::steady_clock::now();
    if (!pipeline_scheduler_detail::TickAlgorithmObject(
          object,
          context,
          allow_tick,
          is_ready,
          true,
          &runtime_state)) {
      runtime_state.algorithm_to_agent_signal.stop_requested = true;
      pipeline_scheduler_detail::SetPipelineStageRuntimeReason(
        pipeline_stage_runtime_stats,
        object.algorithm_profile.algorithm_name,
        "Execution failed.");
      pipeline_scheduler_detail::MergeAlgorithmToAgentSignal(
        runtime_state.algorithm_to_agent_signal,
        out_pipeline_signal);
      set_error("Mounted pipeline stage execution failed.");
      *out_pipeline_processing_failed = true;
      return false;
    }
    const auto stage_end = std::chrono::steady_clock::now();
    pipeline_scheduler_detail::AddPipelineStageRuntimeElapsed(
      pipeline_stage_runtime_stats,
      object.algorithm_profile.algorithm_name,
      std::chrono::duration<float>(stage_end - stage_begin).count());
    pipeline_scheduler_detail::MergeAlgorithmToAgentSignal(
      runtime_state.algorithm_to_agent_signal,
      out_pipeline_signal);
  }

  for (size_t index : executable_indices) {
    ::algorithm_management::AlgorithmObject& object = algorithm_objects[index];
    const algorithm::AlgorithmContainerSet* source_container_set = object.container_set();
    if (!source_container_set) {
      ALGORITHM_SCHEDULER_ASSERT(
        false,
        "Pipeline stage source container set is unavailable.");
      updated_runtime_states[index].algorithm_to_agent_signal.stop_requested = true;
      pipeline_scheduler_detail::SetPipelineStageRuntimeReason(
        pipeline_stage_runtime_stats,
        object.algorithm_profile.algorithm_name,
        "Egress failed: source container set is unavailable.");
      pipeline_scheduler_detail::MergeAlgorithmToAgentSignal(
        updated_runtime_states[index].algorithm_to_agent_signal,
        out_pipeline_signal);
      set_error("Pipeline stage source container set is unavailable.");
      *out_pipeline_processing_failed = true;
      return false;
    }

    std::string egress_error_message;
    algorithm_support::PipelineStageBridge bridge(object.runtime_transfer_map);
    const auto stage_begin = std::chrono::steady_clock::now();
    const size_t stage_offset = index - begin_index;
    const bool is_last_stage = stage_offset + 1u == pipeline_stage_count;
    if (is_last_stage) {
      if (pipeline_scheduler_detail::CollectPipelineExitReflectionSnapshot(
            object.pipeline_name.empty() ? object.algorithm_profile.algorithm_name : object.pipeline_name,
            *source_container_set,
            &updated_runtime_states[index].reflection_snapshot)) {
        updated_runtime_states[index].reflection_snapshot_cached = false;
        pipeline_state.exit_reflection_snapshot = updated_runtime_states[index].reflection_snapshot;
        pipeline_state.exit_reflection_snapshot_valid = true;
      } else {
        updated_runtime_states[index].reflection_snapshot.Clear();
        pipeline_state.exit_reflection_snapshot.Clear();
        pipeline_state.exit_reflection_snapshot_valid = false;
      }
      if (pipeline_state.topology == ::algorithm_management::AlgorithmPipelineTopology::Circular) {
        const bool emit_loopback_ok = pipeline_uses_inter_stage_buffer
          ? bridge.EmitToNextStage(
              object.algorithm_profile.algorithm_name,
              *source_container_set,
              &primary_lane_state->inter_stage_buffer,
              &stage_container_sets,
              &egress_error_message)
          : bridge.EmitToNextStage(
              object.algorithm_profile.algorithm_name,
              *source_container_set,
              &stage_container_sets,
              &egress_error_message);
        if (!emit_loopback_ok) {
          const std::string failure_message = egress_error_message.empty()
            ? std::string("Failed to emit circular pipeline stage output.")
            : std::move(egress_error_message);
          ALGORITHM_SCHEDULER_ASSERT(false, failure_message.c_str());
          updated_runtime_states[index].algorithm_to_agent_signal.stop_requested = true;
          pipeline_scheduler_detail::SetPipelineStageRuntimeReason(
            pipeline_stage_runtime_stats,
            object.algorithm_profile.algorithm_name,
            "Egress failed: " + failure_message);
          pipeline_scheduler_detail::MergeAlgorithmToAgentSignal(
            updated_runtime_states[index].algorithm_to_agent_signal,
            out_pipeline_signal);
          set_error(failure_message);
          *out_pipeline_processing_failed = true;
          return false;
        }
        std::string loopback_error_message;
        if (!pipeline_scheduler_detail::CopyPipelineCircularLoopback(
              *source_container_set,
              object.runtime_transfer_map->pipeline_shared_variable_count,
              object.runtime_transfer_map->pipeline_shared_array_count,
              &algorithm_objects[begin_index],
              &loopback_error_message)) {
          const std::string failure_message = loopback_error_message.empty()
            ? std::string("Failed to emit circular pipeline loopback output.")
            : std::move(loopback_error_message);
          ALGORITHM_SCHEDULER_ASSERT(false, failure_message.c_str());
          updated_runtime_states[index].algorithm_to_agent_signal.stop_requested = true;
          pipeline_scheduler_detail::SetPipelineStageRuntimeReason(
            pipeline_stage_runtime_stats,
            object.algorithm_profile.algorithm_name,
            "Egress failed: " + failure_message);
          pipeline_scheduler_detail::MergeAlgorithmToAgentSignal(
            updated_runtime_states[index].algorithm_to_agent_signal,
            out_pipeline_signal);
          set_error(failure_message);
          *out_pipeline_processing_failed = true;
          return false;
        }
        next_stage_has_data.front() = true;
      }
    } else {
      const bool emit_ok = pipeline_uses_inter_stage_buffer
        ? bridge.EmitToNextStage(
            object.algorithm_profile.algorithm_name,
            *source_container_set,
            &primary_lane_state->inter_stage_buffer,
            &stage_container_sets,
            &egress_error_message)
        : bridge.EmitToNextStage(
            object.algorithm_profile.algorithm_name,
            *source_container_set,
            &stage_container_sets,
            &egress_error_message);
      if (!emit_ok) {
        const std::string failure_message = egress_error_message.empty()
          ? std::string("Failed to emit pipeline stage output.")
          : std::move(egress_error_message);
        ALGORITHM_SCHEDULER_ASSERT(false, failure_message.c_str());
        updated_runtime_states[index].algorithm_to_agent_signal.stop_requested = true;
        pipeline_scheduler_detail::SetPipelineStageRuntimeReason(
          pipeline_stage_runtime_stats,
          object.algorithm_profile.algorithm_name,
          "Egress failed: " + failure_message);
        pipeline_scheduler_detail::MergeAlgorithmToAgentSignal(
          updated_runtime_states[index].algorithm_to_agent_signal,
          out_pipeline_signal);
        set_error(failure_message);
        *out_pipeline_processing_failed = true;
        return false;
      }
      next_stage_has_data[stage_offset + 1u] = true;
    }

    std::string egress_debug_error_message;
    if (!bridge.CaptureEgressDebugSet(
          object.pipeline_name,
          object.algorithm_profile.algorithm_name,
          *source_container_set,
          stage_container_sets,
          &updated_runtime_states[index].bridge_debug_set,
          &egress_debug_error_message)) {
      const std::string failure_message = egress_debug_error_message.empty()
        ? std::string("Failed to capture pipeline egress debug state.")
        : std::move(egress_debug_error_message);
      ALGORITHM_SCHEDULER_ASSERT(false, failure_message.c_str());
      updated_runtime_states[index].algorithm_to_agent_signal.stop_requested = true;
      pipeline_scheduler_detail::SetPipelineStageRuntimeReason(
        pipeline_stage_runtime_stats,
        object.algorithm_profile.algorithm_name,
        "Egress debug capture failed: " + failure_message);
      pipeline_scheduler_detail::MergeAlgorithmToAgentSignal(
        updated_runtime_states[index].algorithm_to_agent_signal,
        out_pipeline_signal);
      set_error(failure_message);
      *out_pipeline_processing_failed = true;
      return false;
    }

    std::string reset_error_message;
    if (!pipeline_scheduler_detail::ClearPipelineExternalWriteResetContainers(
          object,
          const_cast<algorithm::AlgorithmContainerSet*>(source_container_set),
          &reset_error_message)) {
      const std::string failure_message = reset_error_message.empty()
        ? std::string("Failed to clear pipeline external-write reset containers.")
        : std::move(reset_error_message);
      ALGORITHM_SCHEDULER_ASSERT(false, failure_message.c_str());
      updated_runtime_states[index].algorithm_to_agent_signal.stop_requested = true;
      pipeline_scheduler_detail::SetPipelineStageRuntimeReason(
        pipeline_stage_runtime_stats,
        object.algorithm_profile.algorithm_name,
        "External-write reset failed: " + failure_message);
      pipeline_scheduler_detail::MergeAlgorithmToAgentSignal(
        updated_runtime_states[index].algorithm_to_agent_signal,
        out_pipeline_signal);
      set_error(failure_message);
      *out_pipeline_processing_failed = true;
      return false;
    }

    const auto stage_end = std::chrono::steady_clock::now();
    pipeline_scheduler_detail::AddPipelineStageRuntimeElapsed(
      pipeline_stage_runtime_stats,
      object.algorithm_profile.algorithm_name,
      std::chrono::duration<float>(stage_end - stage_begin).count());
  }

  pipeline_scheduler_detail::CommitPipelineStageStateToPrimaryLane(&pipeline_state, next_stage_has_data);
  const uint64_t current_signature =
    pipeline_scheduler_detail::HashPipelineGroupState(
      algorithm_objects,
      executable_indices,
      pipeline_state.current_lane_id,
      pipeline_scheduler_detail::CountValidPipelineLanes(pipeline_state),
      pipeline_state.stage_has_data,
      pipeline_state.pending_stage0_submissions.size(),
      pipeline_state.stage0_saturated,
      updated_runtime_states,
      begin_index,
      end_index);
  const bool signature_unchanged =
    previous_progress_state.signature_valid &&
    previous_progress_state.signature == current_signature;
  pipeline_scheduler_detail::UpdatePipelineGroupProgressState(
    previous_progress_state,
    current_signature,
    true,
    context.dt_seconds,
    &progress_state);
  if (signature_unchanged) {
    progress_state.stall_reason =
      "Pipeline signature did not change after execute and bridge output completed.";
    for (size_t index : executable_indices) {
      ::algorithm_management::AlgorithmObject& object = algorithm_objects[index];
      pipeline_scheduler_detail::SetPipelineStageRuntimeReason(
        pipeline_stage_runtime_stats,
        object.algorithm_profile.algorithm_name,
        "Stage executed, but the observable pipeline state did not change.");
    }
  }
  commit_progress_state(std::move(progress_state));

  {
    std::lock_guard<std::mutex> lock(mutex_);
    pipeline_runtime_states_[root_object.pipeline_name][owner_agent_name] = std::move(pipeline_state);
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

inline bool AlgorithmScheduler::ReplayMountedPipelineStageBridgeDebug(
  std::vector<::algorithm_management::AlgorithmObject>* mounted_objects,
  size_t index,
  const ::agent::AgentTickContext& context,
  std::vector<::algorithm_management::AgentAlgorithmRuntimeState>* inout_runtime_states,
  std::string* out_error_message) {
  if (!mounted_objects || !inout_runtime_states) {
    if (out_error_message) {
      *out_error_message = "Pipeline bridge replay received a null output pointer.";
    }
    return false;
  }
  if (index >= mounted_objects->size() || index >= inout_runtime_states->size()) {
    if (out_error_message) {
      *out_error_message = "Selected algorithm runtime state is unavailable.";
    }
    return false;
  }

  ::algorithm_management::AlgorithmObject& object = (*mounted_objects)[index];
  ::algorithm_management::AgentAlgorithmRuntimeState& runtime_state = (*inout_runtime_states)[index];
  if (!object.pipeline_stage) {
    if (out_error_message) {
      *out_error_message = "Selected algorithm is not a pipeline stage.";
    }
    return false;
  }
  if (!runtime_state.bridge_debug_set.valid || !runtime_state.bridge_debug_set.has_stage_input_container_set) {
    if (out_error_message) {
      *out_error_message = "Pipeline bridge debug input is unavailable for the selected stage.";
    }
    return false;
  }

  runtime_state.bridge_debug_set.replay_output_container_set = {};
  runtime_state.bridge_debug_set.replay_debug_state = {};
  runtime_state.bridge_debug_set.replay_reflection_snapshot.Clear();
  runtime_state.bridge_debug_set.replay_algorithm_to_agent_signal = {};
  runtime_state.bridge_debug_set.has_replay_output_container_set = false;
  runtime_state.bridge_debug_set.replay_valid = false;

  algorithm::AlgorithmContainerSet replay_container_set{};
  algorithm::CopyAlgorithmContainerSet(
    runtime_state.bridge_debug_set.stage_input_container_set,
    &replay_container_set);

  std::string submit_error_message;
  if (!SubmitAlgorithmObject(
        object,
        context,
        runtime_state.agent_to_algorithm_signal,
        &replay_container_set,
        &runtime_state.bridge_debug_set.replay_algorithm_to_agent_signal,
        &runtime_state.bridge_debug_set.replay_debug_state,
        &submit_error_message)) {
    if (out_error_message) {
      *out_error_message = submit_error_message.empty()
        ? "Pipeline bridge debug replay execution failed."
        : std::move(submit_error_message);
    }
    ALGORITHM_SCHEDULER_ASSERT(
      false,
      out_error_message ? out_error_message->c_str() : "Pipeline bridge debug replay execution failed.");
    return false;
  }

  ::algorithm_management::AlgorithmPackageDebugState collected_debug_state{};
  pipeline_scheduler_detail::CollectDebugState(object, &collected_debug_state);
  runtime_state.bridge_debug_set.replay_debug_state.signals.insert(
    runtime_state.bridge_debug_set.replay_debug_state.signals.end(),
    collected_debug_state.signals.begin(),
    collected_debug_state.signals.end());

  if (object.algorithm_reflector && !object.algorithm_reflector->empty()) {
    if (!pipeline_scheduler_detail::CollectReflectionSnapshot(
          object,
          replay_container_set,
          &runtime_state.bridge_debug_set.replay_reflection_snapshot)) {
      if (out_error_message) {
        *out_error_message = "Pipeline bridge debug replay reflection collection failed.";
      }
      ALGORITHM_SCHEDULER_ASSERT(false, "Pipeline bridge debug replay reflection collection failed.");
      return false;
    }
  }

  algorithm::CopyAlgorithmContainerSet(
    replay_container_set,
    &runtime_state.bridge_debug_set.replay_output_container_set);
  runtime_state.bridge_debug_set.has_replay_output_container_set = true;
  runtime_state.bridge_debug_set.replay_valid = true;
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
}

inline bool AlgorithmScheduler::TryGetPipelineRegistration(
  const std::string& pipeline_name,
  CpuPipelineRegistration* out_registration) const {
  if (!out_registration || pipeline_name.empty()) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = pipeline_registrations_.find(pipeline_name);
  if (found == pipeline_registrations_.end()) {
    return false;
  }
  *out_registration = found->second;
  return true;
}

inline bool AlgorithmScheduler::TryGetPipelineRuntime(
  const std::string& pipeline_name,
  const std::string& owner_agent_name,
  CpuPipelineRuntimeState* out_runtime_state) const {
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
  const CpuPipelineRuntimeState& runtime_state,
  std::string* out_error_message) {
  std::lock_guard<std::mutex> lock(mutex_);
  pipeline_runtime_states_[pipeline_name][owner_agent_name] = runtime_state;
  if (out_error_message) {
    out_error_message->clear();
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
