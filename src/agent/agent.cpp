#include "agent/agent.h"

#include "algorithm_management/algorithm_manager.h"

#include <chrono>
#include <cstdint>
#include <cassert>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace agent {

namespace {

#ifndef NDEBUG
#define DEBUG_TOOL_ASSERT(condition, message) assert((condition) && (message))
#else
#define DEBUG_TOOL_ASSERT(condition, message) ((void)0)
#endif

constexpr float kPipelineNoProgressStallSeconds = 1.0f;
constexpr uint64_t kFnvOffsetBasis64 = 1469598103934665603ull;
constexpr uint64_t kFnvPrime64 = 1099511628211ull;

uint64_t _HashBytes(uint64_t hash, const void* data, size_t size) {
  const auto* bytes = static_cast<const unsigned char*>(data);
  for (size_t i = 0; i < size; ++i) {
    hash ^= static_cast<uint64_t>(bytes[i]);
    hash *= kFnvPrime64;
  }
  return hash;
}

uint64_t _HashString(uint64_t hash, const std::string& value) {
  hash = _HashBytes(hash, value.data(), value.size());
  const char terminator = '\0';
  return _HashBytes(hash, &terminator, sizeof(terminator));
}

uint64_t _HashU64(uint64_t hash, uint64_t value) {
  return _HashBytes(hash, &value, sizeof(value));
}

uint64_t _HashBool(uint64_t hash, bool value) {
  const uint8_t byte_value = value ? 1u : 0u;
  return _HashBytes(hash, &byte_value, sizeof(byte_value));
}

uint64_t _HashSignal(uint64_t hash, const AlgorithmToAgentSignal& signal) {
  hash = _HashBool(hash, signal.intervention_applied);
  hash = _HashBool(hash, signal.pause_requested);
  hash = _HashBool(hash, signal.stop_requested);
  hash = _HashBool(hash, signal.intervention_needed);
  hash = _HashBool(hash, signal.reflection_collection_requested);
  hash = _HashU64(hash, static_cast<uint64_t>(signal.control_bits));
  return hash;
}

uint64_t _HashSignal(uint64_t hash, const AgentToAlgorithmSignal& signal) {
  hash = _HashBool(hash, signal.needs_intervention);
  hash = _HashBool(hash, signal.pause_requested);
  hash = _HashBool(hash, signal.stop_requested);
  hash = _HashBool(hash, signal.reflection_collection_requested);
  hash = _HashU64(hash, static_cast<uint64_t>(signal.control_bits));
  return hash;
}

uint64_t _HashContainer(uint64_t hash, const algorithm::AlgorithmContainer& container) {
  hash = _HashString(hash, container.name);
  hash = _HashU64(hash, static_cast<uint64_t>(container.storage_kind));
  hash = _HashU64(hash, static_cast<uint64_t>(container.element_count));
  hash = _HashU64(hash, static_cast<uint64_t>(container.element_stride));
  hash = _HashBool(hash, container.hidden);
  hash = _HashU64(hash, static_cast<uint64_t>(container.bytes.size()));
  if (!container.bytes.empty()) {
    hash = _HashBytes(hash, container.bytes.data(), container.bytes.size());
  }
  return hash;
}

uint64_t _HashContainerSet(uint64_t hash, const algorithm::AlgorithmContainerSet& container_set) {
  hash = _HashString(hash, container_set.algorithm_name);
  hash = _HashString(hash, container_set.standard_layout.layout_name);
  hash = _HashString(hash, container_set.standard_layout.layout_kind);
  hash = _HashU64(hash, static_cast<uint64_t>(container_set.standard_layout.variable_count));
  hash = _HashU64(hash, static_cast<uint64_t>(container_set.standard_layout.array_count));
  hash = _HashString(hash, container_set.standard_layout.variable_prefix);
  hash = _HashString(hash, container_set.standard_layout.array_prefix);
  for (const auto& container : container_set.arrays) {
    hash = _HashContainer(hash, container);
  }
  for (const auto& container : container_set.temporary_registers) {
    hash = _HashContainer(hash, container);
  }
  for (const auto& container : container_set.temporary_caches) {
    hash = _HashContainer(hash, container);
  }
  for (const auto& container : container_set.hidden_containers) {
    hash = _HashContainer(hash, container);
  }
  return hash;
}

uint64_t _HashPipelineGroupState(
  const std::vector<AlgorithmObject>& algorithm_objects,
  const std::vector<size_t>& executable_indices,
  const std::vector<AgentAlgorithmRuntimeState>& runtime_states,
  size_t begin_index,
  size_t end_index) {
  uint64_t hash = kFnvOffsetBasis64;
  for (size_t index = begin_index; index < end_index; ++index) {
    const AlgorithmObject& object = algorithm_objects[index];
    hash = _HashString(hash, object.algorithm_profile.algorithm_name);
    hash = _HashU64(hash, static_cast<uint64_t>(object.pipeline_stage_index));
    hash = _HashU64(hash, static_cast<uint64_t>(object.pipeline_stage_count));
    const algorithm::AlgorithmContainerSet* container_set = object.container_set();
    if (container_set) {
      hash = _HashContainerSet(hash, *container_set);
    }
    if (index < runtime_states.size()) {
      hash = _HashSignal(hash, runtime_states[index].agent_to_algorithm_signal);
      hash = _HashSignal(hash, runtime_states[index].algorithm_to_agent_signal);
    }
  }
  for (size_t index : executable_indices) {
    hash = _HashU64(hash, static_cast<uint64_t>(index));
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
  std::vector<AlgorithmPipelineStageRuntimeStat> stage_runtime_stats;
};

AlgorithmPipelineStageRuntimeStat* _FindPipelineStageRuntimeStat(
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

void _SetPipelineStageRuntimeReason(
  std::vector<AlgorithmPipelineStageRuntimeStat>* stage_runtime_stats,
  const std::string& stage_name,
  std::string reason) {
  AlgorithmPipelineStageRuntimeStat* stage_stat =
    _FindPipelineStageRuntimeStat(stage_runtime_stats, stage_name);
  if (!stage_stat) {
    return;
  }
  stage_stat->reason = std::move(reason);
}

void _AddPipelineStageRuntimeElapsed(
  std::vector<AlgorithmPipelineStageRuntimeStat>* stage_runtime_stats,
  const std::string& stage_name,
  float elapsed_seconds) {
  AlgorithmPipelineStageRuntimeStat* stage_stat =
    _FindPipelineStageRuntimeStat(stage_runtime_stats, stage_name);
  if (!stage_stat) {
    return;
  }
  stage_stat->elapsed_seconds += elapsed_seconds;
}

std::string _BuildPipelineStageIdleReason(
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

void _CollectDebugState(
  const AlgorithmObject& object,
  AlgorithmPackageDebugState* out_debug_state) {
  if (out_debug_state) {
    *out_debug_state = {};
  }

  if (auto* complex_support = dynamic_cast<IComplexAlgorithmPackageSupport*>(object.reflector.get())) {
    if (out_debug_state) {
      complex_support->CollectDebugState(out_debug_state);
    }
  }
}

bool _CollectReflectionSnapshot(
  const AlgorithmObject& object,
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

bool _SignalBlocksTick(
  const AlgorithmObject& object,
  const AgentToAlgorithmSignal& signal) {
  if (signal.stop_requested || signal.pause_requested) {
    return true;
  }
  if (!signal.needs_intervention) {
    return false;
  }
  return !object.intervention || object.intervention->SupportsIntervention();
}

bool _IsPipelineStage(const AlgorithmObject& object) {
  return object.pipeline_stage;
}

bool _FindPipelineGroupRange(
  const std::vector<AlgorithmObject>& algorithm_objects,
  size_t index,
  size_t* out_begin_index,
  size_t* out_end_index) {
  if (index >= algorithm_objects.size()) {
    return false;
  }
  const AlgorithmObject& anchor = algorithm_objects[index];
  if (!_IsPipelineStage(anchor)) {
    return false;
  }

  size_t begin_index = index;
  while (begin_index > 0u) {
    const AlgorithmObject& previous = algorithm_objects[begin_index - 1u];
    if (!_IsPipelineStage(previous) ||
        previous.pipeline_name != anchor.pipeline_name ||
        previous.pipeline_stage_index + 1u != algorithm_objects[begin_index].pipeline_stage_index) {
      break;
    }
    --begin_index;
  }

  size_t end_index = index + 1u;
  while (end_index < algorithm_objects.size()) {
    const AlgorithmObject& next = algorithm_objects[end_index];
    if (!_IsPipelineStage(next) ||
        next.pipeline_name != anchor.pipeline_name ||
        algorithm_objects[end_index - 1u].pipeline_stage_index + 1u != next.pipeline_stage_index) {
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

void _UpdatePipelineGroupProgressState(
  const PipelineGroupProgressState& previous_state,
  uint64_t current_signature,
  bool signature_valid,
  float dt_seconds,
  PipelineGroupProgressState* out_state) {
  if (!out_state) {
    return;
  }
  *out_state = previous_state;
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

void _ResetRuntimeStateBase(
  const AgentAlgorithmRuntimeState& previous_runtime_state,
  AgentAlgorithmRuntimeState* runtime_state) {
  if (!runtime_state) {
    return;
  }
  *runtime_state = previous_runtime_state;
  runtime_state->algorithm_to_agent_signal = {};
  runtime_state->debug_state = {};
  if (!runtime_state->reflection_snapshot_cached) {
    runtime_state->reflection_snapshot.Clear();
  }
}

bool _PipelineGroupIsExecutable(
  const std::vector<size_t>& executable_indices) {
  return !executable_indices.empty();
}

void _MergeAlgorithmToAgentSignal(
  const AlgorithmToAgentSignal& source_signal,
  AlgorithmToAgentSignal* out_target_signal) {
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

bool _TickAlgorithmObject(
  AlgorithmObject& object,
  const AgentTickContext& context,
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
        _SignalBlocksTick(object, runtime_state->agent_to_algorithm_signal) &&
        runtime_state->agent_to_algorithm_signal.needs_intervention;
      runtime_state->algorithm_to_agent_signal.reflection_collection_requested =
        runtime_state->agent_to_algorithm_signal.reflection_collection_requested;
      runtime_state->algorithm_to_agent_signal.control_bits = runtime_state->agent_to_algorithm_signal.control_bits;
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
  runtime_state->algorithm_to_agent_signal.control_bits = runtime_state->agent_to_algorithm_signal.control_bits;

  std::string submit_error_message;
  if (!algorithm_management::SubmitAlgorithmObject(
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
    DEBUG_TOOL_ASSERT(false, failure_message.c_str());
    runtime_state->algorithm_to_agent_signal.stop_requested = true;
    return false;
  }

  AlgorithmPackageDebugState collected_debug_state{};
  _CollectDebugState(object, &collected_debug_state);
  runtime_state->debug_state.signals.insert(
    runtime_state->debug_state.signals.end(),
    collected_debug_state.signals.begin(),
    collected_debug_state.signals.end());

  if (has_runtime_reflector) {
    const bool should_collect_reflection =
      !capture_reflection_once || !runtime_state->reflection_snapshot_cached;
    if (should_collect_reflection) {
      if (!object.container_set()) {
        DEBUG_TOOL_ASSERT(false, "Runtime reflection snapshot requires a container set.");
        runtime_state->algorithm_to_agent_signal.stop_requested = true;
        return false;
      }
      if (!_CollectReflectionSnapshot(
            object,
            *object.container_set(),
            &runtime_state->reflection_snapshot)) {
        DEBUG_TOOL_ASSERT(false, "Runtime reflection snapshot could not be collected.");
        runtime_state->algorithm_to_agent_signal.stop_requested = true;
        return false;
      }
      runtime_state->algorithm_to_agent_signal.reflection_collection_requested = true;
      if (capture_reflection_once) {
        runtime_state->reflection_snapshot_cached = true;
      }
    }
  } else if (runtime_state->agent_to_algorithm_signal.reflection_collection_requested) {
    DEBUG_TOOL_ASSERT(false, "Reflection was requested but the algorithm has no runtime reflector.");
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

bool _BuildPipelineStageContainerSets(
  const std::vector<AlgorithmObject>& algorithm_objects,
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
    const AlgorithmObject& object = algorithm_objects[index];
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

struct BuiltAlgorithmMount {
  AlgorithmObject object{};
  std::shared_ptr<algorithm::AlgorithmContainerSet> container_set{};
  std::string error_message;
  bool ok{false};
};

std::string _StandardLayoutKey(const algorithm::AlgorithmContainerSet& container_set) {
  if (container_set.standard_layout.enabled()) {
    return container_set.standard_layout.layout_name;
  }
  return container_set.algorithm_name;
}

BuiltAlgorithmMount _BuildAlgorithmMount(
  const std::string& algorithm_name,
  const std::vector<AlgorithmResourceBinding>& resource_bindings,
  const std::vector<AlgorithmDescriptorValue>& descriptor_values,
  AlgorithmMountMode mount_mode,
  AlgorithmExecutionPreference execution_preference,
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* standard_shared_container_sets) {
  BuiltAlgorithmMount result{};

  std::string error_message;
  AlgorithmObject object{};
  if (!CreateAlgorithmObjectByName(algorithm_name, &object, &error_message)) {
    result.error_message = error_message.empty()
      ? ("Failed to create algorithm object for '" + algorithm_name + "'.")
      : std::move(error_message);
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
    mount_mode == AlgorithmMountMode::StandardContainer &&
    container_set_handle->standard_layout.enabled() &&
    standard_shared_container_sets;
  const bool cache_shared_standard_container = use_shared_standard_container;
  std::string shared_key;

  if (use_shared_standard_container) {
    shared_key = _StandardLayoutKey(*container_set_handle);
    if (!shared_key.empty()) {
      auto found = standard_shared_container_sets->find(shared_key);
      if (found != standard_shared_container_sets->end() && found->second) {
        container_set_handle = found->second;
      }
    }
  }

  if (!algorithm_management::FinalizeAlgorithmObject(
        object,
        container_set_handle.get(),
        &error_message)) {
    result.error_message = error_message.empty()
      ? ("Failed to finalize algorithm inputs for '" + algorithm_name + "'.")
      : std::move(error_message);
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

}  // namespace

bool CreateAlgorithmObjectByName(
  const std::string& algorithm_name,
  AlgorithmObject* out_group,
  std::string* out_error_message) {
  if (!out_group) {
    if (out_error_message) {
      *out_error_message = "AlgorithmObject output pointer is null.";
    }
    return false;
  }

  algorithm::AlgorithmPackageLocation package_location{};
  std::string location_error_message;
  if (!algorithm_management::TryResolveAlgorithmPackageLocation(
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

  return algorithm_management::CreateAlgorithmObjectFromLocation(package_location, out_group, out_error_message);
}

bool Agent::Init(AgentInitConfig config) {
  agent_name_ = std::move(config.agent_name);
  algorithm_objects_ = std::move(config.algorithm_objects);
  algorithm_runtime_states_.clear();
  algorithm_assembly_states_.clear();
  standard_shared_container_sets_.clear();
  algorithm_runtime_states_.reserve(algorithm_objects_.size());
  algorithm_assembly_states_.reserve(algorithm_objects_.size());
  for (const AlgorithmObject& object : algorithm_objects_) {
    AgentAlgorithmRuntimeState runtime_state{};
    runtime_state.algorithm_name = object.algorithm_profile.algorithm_name;
    algorithm_runtime_states_.push_back(std::move(runtime_state));
    algorithm_assembly_states_.push_back(AlgorithmAssemblyState::Pending);
  }
  initialized_ = true;
  return true;
}

bool Agent::MountAlgorithm(
  const std::string& algorithm_name,
  const std::vector<AlgorithmResourceBinding>& resource_bindings,
  const std::vector<AlgorithmDescriptorValue>& descriptor_values,
  size_t* out_index,
  std::string* out_error_message,
  AlgorithmMountMode mount_mode,
  AlgorithmExecutionPreference execution_preference) {
  auto set_error = [&](const std::string& message) {
    if (out_error_message) {
      *out_error_message = message;
    }
  };

  if (!initialized_) {
    set_error("Agent is not initialized.");
    return false;
  }

  BuiltAlgorithmMount built_mount = _BuildAlgorithmMount(
    algorithm_name,
    resource_bindings,
    descriptor_values,
    mount_mode,
    execution_preference,
    &standard_shared_container_sets_);
  if (!built_mount.ok) {
    set_error(built_mount.error_message);
    return false;
  }

  size_t algorithm_index = 0u;
  if (!AppendAlgorithmObject(std::move(built_mount.object), &algorithm_index)) {
    set_error("Failed to append algorithm object to agent.");
    return false;
  }
  if (!BeginAlgorithmAssembly(algorithm_index)) {
    DEBUG_TOOL_ASSERT(false, "Failed to begin algorithm assembly.");
    MarkAlgorithmAssemblyFailed(algorithm_index);
    set_error("Failed to begin algorithm assembly.");
    return false;
  }

  AlgorithmObject* mounted_algorithm_object = algorithm_object(algorithm_index);
  if (!mounted_algorithm_object) {
    DEBUG_TOOL_ASSERT(false, "Failed to access algorithm object.");
    MarkAlgorithmAssemblyFailed(algorithm_index);
    set_error("Failed to access algorithm object.");
    return false;
  }
  mounted_algorithm_object->SetContainerSet(std::move(built_mount.container_set));
  MarkAlgorithmAssemblyReady(algorithm_index);

  AgentAlgorithmRuntimeState* runtime_state = algorithm_runtime_state(algorithm_index);
  if (runtime_state) {
    AlgorithmReflectionSnapshot reflection_snapshot{};
    if (CollectAlgorithmReflection(algorithm_index, &reflection_snapshot)) {
      runtime_state->reflection_snapshot = std::move(reflection_snapshot);
    }
    if (mounted_algorithm_object->algorithm_reflector) {
      DEBUG_TOOL_ASSERT(
        runtime_state->reflection_snapshot.valid,
        "Mounted algorithm reflector did not produce a valid reflection snapshot.");
    }
  }

  if (out_index) {
    *out_index = algorithm_index;
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool Agent::MountPipelineAlgorithm(
  const std::string& pipeline_name,
  const std::vector<AlgorithmPipelineStageSubmission>& stage_submissions,
  size_t* out_index,
  std::string* out_error_message,
  AlgorithmExecutionPreference execution_preference) {
  auto set_error = [&](const std::string& message) {
    if (out_error_message) {
      *out_error_message = message;
    }
  };

  if (!initialized_) {
    set_error("Agent is not initialized.");
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
  if (PipelineNameInUse(pipeline_name)) {
    set_error("Pipeline name is already in use.");
    return false;
  }

  std::vector<BuiltAlgorithmMount> built_stages{};
  built_stages.reserve(stage_submissions.size());
  std::string expected_standard_layout_key{};
  std::unordered_set<std::string> seen_stage_names{};
  for (size_t stage_index = 0; stage_index < stage_submissions.size(); ++stage_index) {
    const AlgorithmPipelineStageSubmission& stage_submission = stage_submissions[stage_index];
    if (stage_submission.stage_name.empty()) {
      set_error("Pipeline stage name must not be empty.");
      return false;
    }
    if (!seen_stage_names.insert(stage_submission.stage_name).second) {
      set_error("Pipeline stage name is duplicated inside the submission: " + stage_submission.stage_name);
      return false;
    }

    BuiltAlgorithmMount built_mount = _BuildAlgorithmMount(
      stage_submission.stage_name,
      stage_submission.resource_bindings,
      stage_submission.descriptor_values,
      AlgorithmMountMode::Pipeline,
      execution_preference,
      &standard_shared_container_sets_);
    if (!built_mount.ok) {
      set_error(built_mount.error_message);
      return false;
    }

    if (!built_mount.container_set || !built_mount.container_set->standard_layout.enabled()) {
      set_error("Pipeline stage standard container layout is unavailable: " + stage_submission.stage_name);
      return false;
    }
    const std::string stage_standard_layout_key = built_mount.container_set->standard_layout.layout_name;
    if (stage_standard_layout_key.empty()) {
      set_error("Pipeline stage standard container layout key is empty: " + stage_submission.stage_name);
      return false;
    }
    if (stage_index == 0u) {
      expected_standard_layout_key = stage_standard_layout_key;
    } else if (stage_standard_layout_key != expected_standard_layout_key) {
      set_error(
        "Pipeline stages must share the same standard container layout: " + stage_submission.stage_name);
      return false;
    }

    built_mount.object.mount_mode = AlgorithmMountMode::Pipeline;
    built_mount.object.pipeline_stage = true;
    built_mount.object.pipeline_name = pipeline_name;
    built_mount.object.pipeline_stage_index = static_cast<uint32_t>(stage_index);
    built_mount.object.pipeline_stage_count = static_cast<uint32_t>(stage_submissions.size());
    built_stages.push_back(std::move(built_mount));
  }

  const size_t pipeline_begin_index = algorithm_objects_.size();
  size_t appended_count = 0u;
  for (; appended_count < built_stages.size(); ++appended_count) {
    size_t stage_index = 0u;
    if (!AppendAlgorithmObject(std::move(built_stages[appended_count].object), &stage_index)) {
      while (appended_count > 0u) {
        --appended_count;
        if (!algorithm_objects_.empty()) {
          RemoveAlgorithm(algorithm_objects_.size() - 1u);
        }
      }
      set_error("Failed to append pipeline stage object.");
      return false;
    }
  }

  for (size_t i = 0; i < built_stages.size(); ++i) {
    const size_t stage_index = pipeline_begin_index + i;
    algorithm_objects_[stage_index].mount_mode = AlgorithmMountMode::Pipeline;
    algorithm_objects_[stage_index].pipeline_stage = true;
    algorithm_objects_[stage_index].pipeline_name = pipeline_name;
    algorithm_objects_[stage_index].pipeline_stage_index = static_cast<uint32_t>(i);
    algorithm_objects_[stage_index].pipeline_stage_count = static_cast<uint32_t>(built_stages.size());
  }

  if (out_index) {
    *out_index = pipeline_begin_index;
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool Agent::PipelineNameInUse(const std::string& pipeline_name) const {
  if (pipeline_name.empty()) {
    return false;
  }
  for (const AlgorithmObject& object : algorithm_objects_) {
    if (object.pipeline_stage && object.pipeline_name == pipeline_name) {
      return true;
    }
  }
  return false;
}

bool Agent::AppendAlgorithmObject(AlgorithmObject object, size_t* out_index) {
  if (!initialized_) {
    return false;
  }

  algorithm_objects_.push_back(std::move(object));
  algorithm_runtime_states_.push_back(AgentAlgorithmRuntimeState{});
  algorithm_runtime_states_.back().algorithm_name = algorithm_objects_.back().algorithm_profile.algorithm_name;
  algorithm_assembly_states_.push_back(AlgorithmAssemblyState::Pending);

  if (out_index) {
    *out_index = algorithm_objects_.size() - 1u;
  }
  return true;
}

bool Agent::RemoveAlgorithm(size_t index) {
  if (!initialized_ || index >= algorithm_objects_.size() || index >= algorithm_runtime_states_.size() ||
      index >= algorithm_assembly_states_.size()) {
    return false;
  }

  size_t erase_begin = index;
  size_t erase_end = index + 1u;
  if (_IsPipelineStage(algorithm_objects_[index])) {
    if (!_FindPipelineGroupRange(algorithm_objects_, index, &erase_begin, &erase_end)) {
      return false;
    }
  }

  algorithm_objects_.erase(
    algorithm_objects_.begin() + static_cast<std::ptrdiff_t>(erase_begin),
    algorithm_objects_.begin() + static_cast<std::ptrdiff_t>(erase_end));
  algorithm_runtime_states_.erase(
    algorithm_runtime_states_.begin() + static_cast<std::ptrdiff_t>(erase_begin),
    algorithm_runtime_states_.begin() + static_cast<std::ptrdiff_t>(erase_end));
  algorithm_assembly_states_.erase(
    algorithm_assembly_states_.begin() + static_cast<std::ptrdiff_t>(erase_begin),
    algorithm_assembly_states_.begin() + static_cast<std::ptrdiff_t>(erase_end));
  return true;
}

void Agent::RefreshInterventionSignals(const AgentTickContext& context) {
  for (size_t i = 0; i < algorithm_objects_.size(); ++i) {
    if (i >= algorithm_assembly_states_.size() || algorithm_assembly_states_[i] != AlgorithmAssemblyState::Ready) {
      if (i < algorithm_runtime_states_.size()) {
        algorithm_runtime_states_[i].agent_to_algorithm_signal = {};
      }
      continue;
    }
    if (i < algorithm_runtime_states_.size() &&
        algorithm_objects_[i].tick_lifetime == AlgorithmTickLifetime::LaunchOnceThenHold &&
        algorithm_runtime_states_[i].launch_once_completed) {
      algorithm_runtime_states_[i].agent_to_algorithm_signal = {};
      continue;
    }
    AgentAlgorithmRuntimeState& runtime_state = algorithm_runtime_states_[i];
    runtime_state.agent_to_algorithm_signal = {};
    if (algorithm_objects_[i].intervention) {
      algorithm_objects_[i].intervention->FillAgentToAlgorithmSignal(
        context,
        &runtime_state.agent_to_algorithm_signal);
    }
  }
}

bool Agent::SubmitAlgorithm(
  const AgentTickContext& context,
  const std::vector<bool>& allow_tick_mask,
  AgentTickResult* out_result) {
  if (!out_result) {
    return false;
  }
  if (!initialized_) {
    return false;
  }

  out_result->algorithm_to_agent_signal = {};
  out_result->algorithm_runtime_states.clear();
  out_result->algorithm_runtime_states.reserve(algorithm_objects_.size());

  std::vector<AgentAlgorithmRuntimeState> updated_runtime_states = algorithm_runtime_states_;
  updated_runtime_states.resize(algorithm_objects_.size());
  for (size_t index = algorithm_runtime_states_.size(); index < updated_runtime_states.size(); ++index) {
    updated_runtime_states[index].algorithm_name = algorithm_objects_[index].algorithm_profile.algorithm_name;
  }
  bool pipeline_processing_failed = false;

  auto process_algorithm_index = [&](size_t index) {
    AlgorithmObject& object = algorithm_objects_[index];
    AgentAlgorithmRuntimeState runtime_state{};
    if (index < algorithm_runtime_states_.size()) {
      runtime_state = algorithm_runtime_states_[index];
    } else {
      runtime_state.algorithm_name = object.algorithm_profile.algorithm_name;
    }
    _ResetRuntimeStateBase(runtime_state, &runtime_state);

    const bool allow_tick = index < allow_tick_mask.size() ? allow_tick_mask[index] : true;
    const bool is_ready =
      index < algorithm_assembly_states_.size() && algorithm_assembly_states_[index] == AlgorithmAssemblyState::Ready;
    const bool execute_now = allow_tick && is_ready;
    if (!_TickAlgorithmObject(object, context, allow_tick, is_ready, execute_now, &runtime_state)) {
      runtime_state.algorithm_to_agent_signal.stop_requested = true;
    }

    _MergeAlgorithmToAgentSignal(runtime_state.algorithm_to_agent_signal, &out_result->algorithm_to_agent_signal);
    updated_runtime_states[index] = std::move(runtime_state);
  };

  auto process_pipeline_stage_group = [&](size_t begin_index, size_t end_index) {
    std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>> stage_container_sets{};
    std::string stage_set_error_message;
    if (!_BuildPipelineStageContainerSets(
          algorithm_objects_,
          begin_index,
          end_index,
          &stage_container_sets,
          &stage_set_error_message)) {
      const std::string failure_message = stage_set_error_message.empty()
        ? std::string("Failed to build pipeline stage container sets.")
        : std::move(stage_set_error_message);
      DEBUG_TOOL_ASSERT(false, failure_message.c_str());
      out_result->algorithm_to_agent_signal.stop_requested = true;
      pipeline_processing_failed = true;
      return;
    }

    PipelineGroupProgressState previous_progress_state{};
    if (begin_index < updated_runtime_states.size()) {
      previous_progress_state.signature = updated_runtime_states[begin_index].pipeline_progress_signature;
      previous_progress_state.signature_valid = updated_runtime_states[begin_index].pipeline_progress_signature_valid;
      previous_progress_state.no_progress_seconds = updated_runtime_states[begin_index].pipeline_no_progress_seconds;
      previous_progress_state.stall_report_requested = updated_runtime_states[begin_index].pipeline_stall_report_requested;
      previous_progress_state.stall_reported = updated_runtime_states[begin_index].pipeline_stall_reported;
      previous_progress_state.stall_reason = updated_runtime_states[begin_index].pipeline_stall_reason;
      previous_progress_state.stage_runtime_stats = updated_runtime_states[begin_index].pipeline_stage_runtime_stats;
    }
    PipelineGroupProgressState progress_state = previous_progress_state;
    progress_state.stall_reason.clear();
    progress_state.stage_runtime_stats.clear();
    progress_state.stage_runtime_stats.reserve(end_index - begin_index);

    std::vector<size_t> executable_indices{};
    executable_indices.reserve(end_index - begin_index);
    for (size_t index = begin_index; index < end_index; ++index) {
      AlgorithmObject& object = algorithm_objects_[index];
      AgentAlgorithmRuntimeState runtime_state{};
      if (index < algorithm_runtime_states_.size()) {
        runtime_state = algorithm_runtime_states_[index];
      } else {
        runtime_state.algorithm_name = object.algorithm_profile.algorithm_name;
      }
      _ResetRuntimeStateBase(runtime_state, &runtime_state);

      progress_state.stage_runtime_stats.push_back(AlgorithmPipelineStageRuntimeStat{
        .stage_name = object.algorithm_profile.algorithm_name,
        .elapsed_seconds = 0.0f,
        .reason = {},
      });
      const bool allow_tick = index < allow_tick_mask.size() ? allow_tick_mask[index] : true;
      const bool is_ready =
        index < algorithm_assembly_states_.size() && algorithm_assembly_states_[index] == AlgorithmAssemblyState::Ready;
      const bool launch_once_then_hold = object.tick_lifetime == AlgorithmTickLifetime::LaunchOnceThenHold;
      const bool launch_once_completed = launch_once_then_hold && runtime_state.launch_once_completed;
      const bool execute_now = allow_tick && is_ready && !launch_once_completed;
      if (!execute_now) {
        _SetPipelineStageRuntimeReason(
          &progress_state.stage_runtime_stats,
          object.algorithm_profile.algorithm_name,
          _BuildPipelineStageIdleReason(allow_tick, is_ready, launch_once_completed));
        if (!_TickAlgorithmObject(object, context, allow_tick, is_ready, false, &runtime_state)) {
          runtime_state.algorithm_to_agent_signal.stop_requested = true;
        }
        _MergeAlgorithmToAgentSignal(runtime_state.algorithm_to_agent_signal, &out_result->algorithm_to_agent_signal);
      } else {
        _SetPipelineStageRuntimeReason(
          &progress_state.stage_runtime_stats,
          object.algorithm_profile.algorithm_name,
          "Stage executed.");
        executable_indices.push_back(index);
      }
      updated_runtime_states[index] = std::move(runtime_state);
    }

    if (!_PipelineGroupIsExecutable(executable_indices)) {
      _UpdatePipelineGroupProgressState(
        previous_progress_state,
        previous_progress_state.signature,
        previous_progress_state.signature_valid,
        context.dt_seconds,
        &progress_state);
      progress_state.stall_reason = "No pipeline stage was executable during this tick.";
      if (begin_index < updated_runtime_states.size()) {
        updated_runtime_states[begin_index].pipeline_progress_signature = progress_state.signature;
        updated_runtime_states[begin_index].pipeline_progress_signature_valid = progress_state.signature_valid;
        updated_runtime_states[begin_index].pipeline_no_progress_seconds = progress_state.no_progress_seconds;
        updated_runtime_states[begin_index].pipeline_stall_report_requested = progress_state.stall_report_requested;
        updated_runtime_states[begin_index].pipeline_stall_reported = progress_state.stall_reported;
        updated_runtime_states[begin_index].pipeline_stall_reason = std::move(progress_state.stall_reason);
        updated_runtime_states[begin_index].pipeline_stage_runtime_stats = std::move(progress_state.stage_runtime_stats);
      }
      return;
    }

    for (size_t index : executable_indices) {
      AlgorithmObject& object = algorithm_objects_[index];
      std::string ingress_error_message;
      algorithm_support::PipelineStageBridge bridge(object.runtime_transfer_map);
      const auto stage_begin = std::chrono::steady_clock::now();
      if (!bridge.IngestFromPreviousStage(
            object.algorithm_profile.algorithm_name,
            stage_container_sets,
            object.mutable_container_set(),
            &ingress_error_message)) {
        const std::string failure_message = ingress_error_message.empty()
          ? std::string("Failed to ingest pipeline stage input.")
          : std::move(ingress_error_message);
        DEBUG_TOOL_ASSERT(false, failure_message.c_str());
        updated_runtime_states[index].algorithm_to_agent_signal.stop_requested = true;
        _SetPipelineStageRuntimeReason(
          &progress_state.stage_runtime_stats,
          object.algorithm_profile.algorithm_name,
          "Ingress failed: " + failure_message);
        _MergeAlgorithmToAgentSignal(updated_runtime_states[index].algorithm_to_agent_signal, &out_result->algorithm_to_agent_signal);
        pipeline_processing_failed = true;
        return;
      }
      const auto stage_end = std::chrono::steady_clock::now();
      const float stage_elapsed_seconds =
        std::chrono::duration<float>(stage_end - stage_begin).count();
      _AddPipelineStageRuntimeElapsed(
        &progress_state.stage_runtime_stats,
        object.algorithm_profile.algorithm_name,
        stage_elapsed_seconds);
    }

    for (size_t index : executable_indices) {
      AlgorithmObject& object = algorithm_objects_[index];
      AgentAlgorithmRuntimeState& runtime_state = updated_runtime_states[index];
      const bool allow_tick = index < allow_tick_mask.size() ? allow_tick_mask[index] : true;
      const bool is_ready =
        index < algorithm_assembly_states_.size() && algorithm_assembly_states_[index] == AlgorithmAssemblyState::Ready;
      const auto stage_begin = std::chrono::steady_clock::now();
      if (!_TickAlgorithmObject(object, context, allow_tick, is_ready, true, &runtime_state)) {
        runtime_state.algorithm_to_agent_signal.stop_requested = true;
        _SetPipelineStageRuntimeReason(
          &progress_state.stage_runtime_stats,
          object.algorithm_profile.algorithm_name,
          "Execution failed.");
        _MergeAlgorithmToAgentSignal(runtime_state.algorithm_to_agent_signal, &out_result->algorithm_to_agent_signal);
        pipeline_processing_failed = true;
        return;
      }
      const auto stage_end = std::chrono::steady_clock::now();
      const float stage_elapsed_seconds =
        std::chrono::duration<float>(stage_end - stage_begin).count();
      _AddPipelineStageRuntimeElapsed(
        &progress_state.stage_runtime_stats,
        object.algorithm_profile.algorithm_name,
        stage_elapsed_seconds);

      _MergeAlgorithmToAgentSignal(runtime_state.algorithm_to_agent_signal, &out_result->algorithm_to_agent_signal);
    }

    for (size_t index : executable_indices) {
      AlgorithmObject& object = algorithm_objects_[index];
      const algorithm::AlgorithmContainerSet* source_container_set = object.container_set();
      if (!source_container_set) {
        DEBUG_TOOL_ASSERT(false, "Pipeline stage source container set is unavailable.");
        updated_runtime_states[index].algorithm_to_agent_signal.stop_requested = true;
        _SetPipelineStageRuntimeReason(
          &progress_state.stage_runtime_stats,
          object.algorithm_profile.algorithm_name,
          "Egress failed: source container set is unavailable.");
        _MergeAlgorithmToAgentSignal(updated_runtime_states[index].algorithm_to_agent_signal, &out_result->algorithm_to_agent_signal);
        pipeline_processing_failed = true;
        return;
      }

      std::string egress_error_message;
      algorithm_support::PipelineStageBridge bridge(object.runtime_transfer_map);
      const auto stage_begin = std::chrono::steady_clock::now();
      if (!bridge.EmitToNextStage(
            object.algorithm_profile.algorithm_name,
            *source_container_set,
            &stage_container_sets,
            &egress_error_message)) {
        const std::string failure_message = egress_error_message.empty()
          ? std::string("Failed to emit pipeline stage output.")
          : std::move(egress_error_message);
        DEBUG_TOOL_ASSERT(false, failure_message.c_str());
        updated_runtime_states[index].algorithm_to_agent_signal.stop_requested = true;
        _SetPipelineStageRuntimeReason(
          &progress_state.stage_runtime_stats,
          object.algorithm_profile.algorithm_name,
          "Egress failed: " + failure_message);
        _MergeAlgorithmToAgentSignal(updated_runtime_states[index].algorithm_to_agent_signal, &out_result->algorithm_to_agent_signal);
        pipeline_processing_failed = true;
        return;
      }
      const auto stage_end = std::chrono::steady_clock::now();
      const float stage_elapsed_seconds =
        std::chrono::duration<float>(stage_end - stage_begin).count();
      _AddPipelineStageRuntimeElapsed(
        &progress_state.stage_runtime_stats,
        object.algorithm_profile.algorithm_name,
        stage_elapsed_seconds);
    }

    const uint64_t current_signature =
      _HashPipelineGroupState(algorithm_objects_, executable_indices, updated_runtime_states, begin_index, end_index);
    const bool signature_unchanged =
      previous_progress_state.signature_valid && previous_progress_state.signature == current_signature;
    _UpdatePipelineGroupProgressState(
      previous_progress_state,
      current_signature,
      true,
      context.dt_seconds,
      &progress_state);
    if (signature_unchanged) {
      progress_state.stall_reason =
        "Pipeline signature did not change after ingest, execute, and emit completed.";
      for (size_t index : executable_indices) {
        AlgorithmObject& object = algorithm_objects_[index];
        _SetPipelineStageRuntimeReason(
          &progress_state.stage_runtime_stats,
          object.algorithm_profile.algorithm_name,
          "Stage executed, but the observable pipeline state did not change.");
      }
    }
    if (begin_index < updated_runtime_states.size()) {
      updated_runtime_states[begin_index].pipeline_progress_signature = progress_state.signature;
      updated_runtime_states[begin_index].pipeline_progress_signature_valid = progress_state.signature_valid;
      updated_runtime_states[begin_index].pipeline_no_progress_seconds = progress_state.no_progress_seconds;
      updated_runtime_states[begin_index].pipeline_stall_report_requested = progress_state.stall_report_requested;
      updated_runtime_states[begin_index].pipeline_stall_reported = progress_state.stall_reported;
      updated_runtime_states[begin_index].pipeline_stall_reason = std::move(progress_state.stall_reason);
      updated_runtime_states[begin_index].pipeline_stage_runtime_stats = std::move(progress_state.stage_runtime_stats);
    }
  };

  size_t index = 0u;
  while (index < algorithm_objects_.size()) {
    const AlgorithmObject& object = algorithm_objects_[index];
    if (!object.pipeline_stage) {
      process_algorithm_index(index);
      ++index;
      continue;
    }

    size_t pipeline_begin_index = 0u;
    size_t pipeline_end_index = 0u;
    if (!_FindPipelineGroupRange(algorithm_objects_, index, &pipeline_begin_index, &pipeline_end_index)) {
      DEBUG_TOOL_ASSERT(false, "Failed to resolve pipeline stage range.");
      out_result->algorithm_to_agent_signal.stop_requested = true;
      break;
    }
    if (pipeline_begin_index != index) {
      ++index;
      continue;
    }

    process_pipeline_stage_group(pipeline_begin_index, pipeline_end_index);
    if (pipeline_processing_failed) {
      break;
    }
    index = pipeline_end_index;
  }

  for (const AgentAlgorithmRuntimeState& runtime_state : updated_runtime_states) {
    out_result->algorithm_runtime_states.push_back(runtime_state);
  }

  algorithm_runtime_states_ = std::move(updated_runtime_states);
  return true;
}

bool Agent::Tick(
  const AgentTickContext& context,
  const std::vector<bool>& allow_tick_mask,
  AgentTickResult* out_result) {
  return SubmitAlgorithm(context, allow_tick_mask, out_result);
}

bool Agent::BeginAlgorithmAssembly(size_t index) {
  if (index >= algorithm_assembly_states_.size()) {
    return false;
  }
  algorithm_assembly_states_[index] = AlgorithmAssemblyState::Assembling;
  return true;
}

void Agent::MarkAlgorithmAssemblyReady(size_t index) {
  if (index >= algorithm_assembly_states_.size()) {
    return;
  }
  algorithm_assembly_states_[index] = AlgorithmAssemblyState::Ready;
}

void Agent::MarkAlgorithmAssemblyFailed(size_t index) {
  if (index >= algorithm_assembly_states_.size()) {
    return;
  }
  algorithm_assembly_states_[index] = AlgorithmAssemblyState::Failed;
}

bool Agent::GetAlgorithmAssemblySlot(size_t index, AlgorithmAssemblySlot* out_slot) {
  if (!out_slot || index >= algorithm_objects_.size() || index >= algorithm_assembly_states_.size()) {
    return false;
  }

  out_slot->index = index;
  out_slot->algorithm_object = &algorithm_objects_[index];
  out_slot->assembly_state = &algorithm_assembly_states_[index];
  return true;
}

bool Agent::CollectAlgorithmReflection(size_t index, AlgorithmReflectionSnapshot* out_snapshot) const {
  if (!out_snapshot || index >= algorithm_objects_.size()) {
    return false;
  }
  const AlgorithmObject& object = algorithm_objects_[index];
  const AlgorithmContainerSet* container_set = algorithm_objects_[index].container_set();
  if (!container_set) {
    return false;
  }
  return _CollectReflectionSnapshot(object, *container_set, out_snapshot);
}

void Agent::Destroy() {
  initialized_ = false;
  agent_name_.clear();
  algorithm_objects_.clear();
  algorithm_runtime_states_.clear();
  algorithm_assembly_states_.clear();
  standard_shared_container_sets_.clear();
}

}  // namespace agent
