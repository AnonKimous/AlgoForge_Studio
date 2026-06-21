#define RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE 1
#include "runtime_systems/job_system.h"
#undef RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE

#include "algorithm_management/algorithm_manager.h"
#include "algorithm_support/algorithm_protocol.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <future>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace runtime_systems {

void ClearGpuRuntime();
bool TryExecuteGpuTick(
  const agent::AlgorithmObject& object,
  algorithm::AlgorithmContainerSet* container_set,
  const agent::AgentTickContext& context,
  std::string* out_error_message);
bool TrySynchronizeGpuTickState(
  const agent::AlgorithmObject& object,
  algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message);

namespace {

#ifndef NDEBUG
#define DEBUG_TOOL_ASSERT(condition, message) assert((condition) && (message))
#else
#define DEBUG_TOOL_ASSERT(condition, message) ((void)0)
#endif

constexpr float kPipelineNoProgressStallSeconds = 1.0f;
constexpr uint64_t kFnvOffsetBasis64 = 1469598103934665603ull;
constexpr uint64_t kFnvPrime64 = 1099511628211ull;

[[noreturn]] void _AbortJobExecution(std::string message) {
  assert(false && "Job execution failed");
  throw std::runtime_error(std::move(message));
}

struct CpuJobCompletion {
  bool ok{false};
  std::string error_message;
};

struct CpuJobTask {
  algorithm_management::AlgorithmJobPriority priority{algorithm_management::AlgorithmJobPriority::High};
  std::shared_ptr<CpuJobCompletion> completion;
  std::shared_ptr<std::promise<void>> done;
  std::function<void(CpuJobCompletion&)> body;
};

bool _HasPipelineStageBufferSlot(
  const algorithm::AlgorithmContainerSet& container_set,
  const std::string& stage_buffer_slot_name);

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

uint64_t _HashSignal(uint64_t hash, const common_data::AlgorithmToAgentSignal& signal) {
  hash = _HashBool(hash, signal.intervention_applied);
  hash = _HashBool(hash, signal.pause_requested);
  hash = _HashBool(hash, signal.stop_requested);
  hash = _HashBool(hash, signal.intervention_needed);
  hash = _HashBool(hash, signal.reflection_collection_requested);
  hash = _HashU64(hash, static_cast<uint64_t>(signal.control_bits));
  return hash;
}

uint64_t _HashSignal(uint64_t hash, const common_data::AgentToAlgorithmSignal& signal) {
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
  const std::vector<algorithm_management::AlgorithmObject>& algorithm_objects,
  const std::vector<size_t>& executable_indices,
  uint64_t current_lane_id,
  size_t valid_lane_count,
  const std::vector<bool>& stage_has_data,
  size_t pending_stage0_submission_count,
  bool stage0_saturated,
  const std::vector<algorithm_management::AgentAlgorithmRuntimeState>& runtime_states,
  size_t begin_index,
  size_t end_index) {
  uint64_t hash = kFnvOffsetBasis64;
  for (size_t index = begin_index; index < end_index; ++index) {
    const algorithm_management::AlgorithmObject& object = algorithm_objects[index];
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
  for (bool has_data : stage_has_data) {
    hash = _HashBool(hash, has_data);
  }
  hash = _HashU64(hash, current_lane_id);
  hash = _HashU64(hash, static_cast<uint64_t>(valid_lane_count));
  hash = _HashU64(hash, static_cast<uint64_t>(pending_stage0_submission_count));
  hash = _HashBool(hash, stage0_saturated);
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
  float total_elapsed_seconds{0.0f};
  std::vector<algorithm_management::AlgorithmPipelineStageRuntimeStat> stage_runtime_stats;
};

algorithm_management::AlgorithmPipelineStageRuntimeStat* _FindPipelineStageRuntimeStat(
  std::vector<algorithm_management::AlgorithmPipelineStageRuntimeStat>* stage_runtime_stats,
  const std::string& stage_name) {
  if (!stage_runtime_stats) {
    return nullptr;
  }
  for (algorithm_management::AlgorithmPipelineStageRuntimeStat& stage_stat : *stage_runtime_stats) {
    if (stage_stat.stage_name == stage_name) {
      return &stage_stat;
    }
  }
  return nullptr;
}

void _SetPipelineStageRuntimeReason(
  std::vector<algorithm_management::AlgorithmPipelineStageRuntimeStat>* stage_runtime_stats,
  const std::string& stage_name,
  std::string reason) {
  algorithm_management::AlgorithmPipelineStageRuntimeStat* stage_stat =
    _FindPipelineStageRuntimeStat(stage_runtime_stats, stage_name);
  if (!stage_stat) {
    return;
  }
  stage_stat->reason = std::move(reason);
}

void _AddPipelineStageRuntimeElapsed(
  std::vector<algorithm_management::AlgorithmPipelineStageRuntimeStat>* stage_runtime_stats,
  const std::string& stage_name,
  float elapsed_seconds) {
  algorithm_management::AlgorithmPipelineStageRuntimeStat* stage_stat =
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

std::string _BuildPipelineStageNoInputReason(bool is_stage0, bool has_pending_stage0_submission) {
  if (is_stage0 && has_pending_stage0_submission) {
    return "Stage0 is waiting for a free execution slot before accepting the next resource batch.";
  }
  if (is_stage0) {
    return "Stage0 is waiting for an external resource batch.";
  }
  return "Stage has no pipeline input token.";
}

void _CollectDebugState(
  const algorithm_management::AlgorithmObject& object,
  algorithm_management::AlgorithmPackageDebugState* out_debug_state) {
  if (out_debug_state) {
    *out_debug_state = {};
  }
  if (auto* complex_support =
        dynamic_cast<algorithm_management::IComplexAlgorithmPackageSupport*>(object.reflector.get())) {
    if (out_debug_state) {
      complex_support->CollectDebugState(out_debug_state);
    }
  }
}

bool _CollectReflectionSnapshot(
  const algorithm_management::AlgorithmObject& object,
  const algorithm::AlgorithmContainerSet& container_set,
  algorithm_management::AlgorithmReflectionSnapshot* out_snapshot) {
  if (!out_snapshot) {
    return false;
  }
  out_snapshot->Clear();
  out_snapshot->algorithm_name = object.algorithm_profile.algorithm_name;
  if (!object.algorithm_reflector) {
    return false;
  }

  for (const auto& [reflection_object_name, binding] :
       object.algorithm_reflector->container_bindings_by_reflection_object_name) {
    for (const std::string& container_name : binding.container_names) {
      const algorithm::AlgorithmContainer* container =
        algorithm::FindAlgorithmContainer(container_set, container_name);
      if (!container) {
        continue;
      }

      algorithm_management::AlgorithmReflectionValue value{};
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

bool _CollectPipelineExitReflectionSnapshot(
  const std::string& algorithm_name,
  const algorithm::AlgorithmContainerSet& container_set,
  algorithm_management::AlgorithmReflectionSnapshot* out_snapshot) {
  if (!out_snapshot) {
    return false;
  }

  out_snapshot->Clear();
  out_snapshot->algorithm_name = algorithm_name.empty() ? container_set.algorithm_name : algorithm_name;

  for (const algorithm::AlgorithmContainer& container : container_set.arrays) {
    algorithm_management::AlgorithmReflectionValue value{};
    value.reflection_object_name = container.name;
    value.container_name = container.name;
    value.filter_name = "pipeline_exit";
    value.storage_kind = container.storage_kind;
    value.bytes.assign(container.bytes.begin(), container.bytes.end());
    out_snapshot->variable_arrays.push_back(std::move(value));
  }
  for (const algorithm::AlgorithmContainer& container : container_set.temporary_registers) {
    algorithm_management::AlgorithmReflectionValue value{};
    value.reflection_object_name = container.name;
    value.container_name = container.name;
    value.filter_name = "pipeline_exit";
    value.storage_kind = container.storage_kind;
    value.bytes.assign(container.bytes.begin(), container.bytes.end());
    out_snapshot->variables.push_back(std::move(value));
  }
  for (const algorithm::AlgorithmContainer& container : container_set.temporary_caches) {
    algorithm_management::AlgorithmReflectionValue value{};
    value.reflection_object_name = container.name;
    value.container_name = container.name;
    value.filter_name = "pipeline_exit";
    value.storage_kind = container.storage_kind;
    value.bytes.assign(container.bytes.begin(), container.bytes.end());
    out_snapshot->variables.push_back(std::move(value));
  }
  for (const algorithm::AlgorithmContainer& container : container_set.hidden_containers) {
    algorithm_management::AlgorithmReflectionValue value{};
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

bool _SignalBlocksTick(
  const algorithm_management::AlgorithmObject& object,
  const common_data::AgentToAlgorithmSignal& signal) {
  if (signal.stop_requested || signal.pause_requested) {
    return true;
  }
  if (!signal.needs_intervention) {
    return false;
  }
  return !object.intervention || object.intervention->SupportsIntervention();
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

void _ResetRuntimeStateBase(
  const algorithm_management::AgentAlgorithmRuntimeState& previous_runtime_state,
  algorithm_management::AgentAlgorithmRuntimeState* runtime_state) {
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

bool _PipelineGroupIsExecutable(const std::vector<size_t>& executable_indices) {
  return !executable_indices.empty();
}

size_t _CountValidPipelineLanes(const CpuPipelineRuntimeState& pipeline_state) {
  size_t valid_lane_count = 0u;
  for (const CpuPipelineLaneRuntimeState& lane_state : pipeline_state.lanes) {
    if (lane_state.valid) {
      ++valid_lane_count;
    }
  }
  return valid_lane_count;
}

bool _TryBuildInitialPipelineLaneRuntimeState(
  const algorithm_management::AlgorithmObject& stage0_object,
  size_t pipeline_stage_count,
  const std::string& owner_agent_name,
  bool loop_lane_active,
  uint64_t lane_id,
  const std::string& stage_buffer_slot_name,
  CpuPipelineLaneRuntimeState* out_lane_state,
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
  if (!_HasPipelineStageBufferSlot(*standard_container_set, stage_buffer_slot_name)) {
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
  out_lane_state->inter_stage_buffer.standard_container_slot_name = stage_buffer_slot_name;
  out_lane_state->inter_stage_buffer.valid = true;
  out_lane_state->valid = true;
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

CpuPipelineLaneRuntimeState* _FindPrimaryPipelineLaneRuntimeState(CpuPipelineRuntimeState* pipeline_state) {
  if (!pipeline_state) {
    return nullptr;
  }
  if (pipeline_state->current_lane_id != 0u) {
    for (CpuPipelineLaneRuntimeState& lane_state : pipeline_state->lanes) {
      if (lane_state.valid && lane_state.lane_id == pipeline_state->current_lane_id) {
        return &lane_state;
      }
    }
  }
  for (CpuPipelineLaneRuntimeState& lane_state : pipeline_state->lanes) {
    if (lane_state.valid) {
      return &lane_state;
    }
  }
  return nullptr;
}

const CpuPipelineLaneRuntimeState* _FindPrimaryPipelineLaneRuntimeState(
  const CpuPipelineRuntimeState& pipeline_state) {
  if (pipeline_state.current_lane_id != 0u) {
    for (const CpuPipelineLaneRuntimeState& lane_state : pipeline_state.lanes) {
      if (lane_state.valid && lane_state.lane_id == pipeline_state.current_lane_id) {
        return &lane_state;
      }
    }
  }
  for (const CpuPipelineLaneRuntimeState& lane_state : pipeline_state.lanes) {
    if (lane_state.valid) {
      return &lane_state;
    }
  }
  return nullptr;
}

void _SyncPipelineLegacyStageStateFromPrimaryLane(
  CpuPipelineRuntimeState* pipeline_state,
  size_t pipeline_stage_count) {
  if (!pipeline_state) {
    return;
  }
  pipeline_state->stage_has_data.assign(pipeline_stage_count, false);
  const CpuPipelineLaneRuntimeState* primary_lane =
    _FindPrimaryPipelineLaneRuntimeState(*pipeline_state);
  if (!primary_lane) {
    return;
  }
  pipeline_state->stage_has_data = primary_lane->stage_has_data;
}

void _CommitPipelineStageStateToPrimaryLane(
  CpuPipelineRuntimeState* pipeline_state,
  const std::vector<bool>& next_stage_has_data) {
  if (!pipeline_state) {
    return;
  }
  CpuPipelineLaneRuntimeState* primary_lane =
    _FindPrimaryPipelineLaneRuntimeState(pipeline_state);
  if (primary_lane) {
    primary_lane->stage_has_data = next_stage_has_data;
    if (!next_stage_has_data.empty()) {
      primary_lane->loop_lane_active = next_stage_has_data.front();
    }
  }
  pipeline_state->stage_has_data = next_stage_has_data;
}

CpuPipelineLaneRuntimeState* _FindPipelineLaneRuntimeStateById(
  CpuPipelineRuntimeState* pipeline_state,
  uint64_t lane_id) {
  if (!pipeline_state || lane_id == 0u) {
    return nullptr;
  }
  for (CpuPipelineLaneRuntimeState& lane_state : pipeline_state->lanes) {
    if (lane_state.valid && lane_state.lane_id == lane_id) {
      return &lane_state;
    }
  }
  return nullptr;
}

void _MergeAlgorithmToAgentSignal(
  const common_data::AlgorithmToAgentSignal& source_signal,
  common_data::AlgorithmToAgentSignal* out_target_signal) {
  if (!out_target_signal) {
    return;
  }
  out_target_signal->intervention_applied =
    out_target_signal->intervention_applied || source_signal.intervention_applied;
  out_target_signal->pause_requested =
    out_target_signal->pause_requested || source_signal.pause_requested;
  out_target_signal->stop_requested =
    out_target_signal->stop_requested || source_signal.stop_requested;
  out_target_signal->intervention_needed =
    out_target_signal->intervention_needed || source_signal.intervention_needed;
  out_target_signal->reflection_collection_requested =
    out_target_signal->reflection_collection_requested || source_signal.reflection_collection_requested;
  out_target_signal->control_bits |= source_signal.control_bits;
}

bool _TickAlgorithmObject(
  algorithm_management::AlgorithmObject& object,
  const algorithm_management::AgentTickContext& context,
  bool allow_tick,
  bool is_ready,
  bool execute_now,
  algorithm_management::AgentAlgorithmRuntimeState* runtime_state) {
  if (!runtime_state) {
    return false;
  }
  runtime_state->algorithm_to_agent_signal = {};
  runtime_state->debug_state = {};

  const bool launch_once_then_hold =
    object.tick_lifetime == algorithm_management::AlgorithmTickLifetime::LaunchOnceThenHold;
  const bool launch_once_completed = launch_once_then_hold && runtime_state->launch_once_completed;
  const bool has_runtime_reflector = object.algorithm_reflector && !object.algorithm_reflector->empty();
  const bool capture_reflection_once =
    has_runtime_reflector &&
    object.algorithm_reflector->refresh_mode ==
      algorithm::AlgorithmReflectionRefreshMode::CaptureOnceAfterCompletion;
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

  algorithm_management::AlgorithmPackageDebugState collected_debug_state{};
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
  const std::vector<algorithm_management::AlgorithmObject>& algorithm_objects,
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
    const algorithm_management::AlgorithmObject& object = algorithm_objects[index];
    if (object.algorithm_profile.algorithm_name.empty()) {
      if (out_error_message) {
        *out_error_message = "Pipeline stage name is empty.";
      }
      return false;
    }
    if (!object.container_set()) {
      if (out_error_message) {
        *out_error_message =
          "Pipeline stage container set is unavailable for '" + object.algorithm_profile.algorithm_name + "'.";
      }
      return false;
    }
    const auto [_, inserted] =
      out_stage_container_sets->emplace(object.algorithm_profile.algorithm_name, object.shared_container_set);
    if (!inserted) {
      if (out_error_message) {
        *out_error_message =
          "Pipeline stage name is duplicated: '" + object.algorithm_profile.algorithm_name + "'.";
      }
      return false;
    }
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool _CopyPipelineCircularLoopback(
  const algorithm::AlgorithmContainerSet& source_container_set,
  algorithm_management::AlgorithmObject* target_stage,
  std::string* out_error_message) {
  if (!target_stage || !target_stage->mutable_container_set()) {
    if (out_error_message) {
      *out_error_message = "Circular pipeline loopback target container set is unavailable.";
    }
    return false;
  }
  algorithm::AlgorithmContainerSet* target_container_set = target_stage->mutable_container_set();
  if (!source_container_set.standard_layout.enabled() || !target_container_set->standard_layout.enabled()) {
    if (out_error_message) {
      *out_error_message = "Circular pipeline loopback requires standard container layouts.";
    }
    return false;
  }
  if (source_container_set.standard_layout.layout_name != target_container_set->standard_layout.layout_name) {
    if (out_error_message) {
      *out_error_message = "Circular pipeline loopback requires identical standard layouts.";
    }
    return false;
  }

  for (uint32_t i = 0; i < source_container_set.standard_layout.variable_count; ++i) {
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
        *out_error_message =
          "Circular pipeline loopback variable slot structure mismatch at '" + slot_name + "'.";
      }
      return false;
    }
    std::memcpy(target_container->bytes.data(), source_container->bytes.data(), source_container->bytes.size());
  }

  for (uint32_t i = 0; i < source_container_set.standard_layout.array_count; ++i) {
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
        *out_error_message =
          "Circular pipeline loopback array slot structure mismatch at '" + slot_name + "'.";
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

size_t _CountLiveCpuPipelineStages(const std::vector<bool>& stage_has_data) {
  size_t live_stage_count = 0u;
  for (bool has_data : stage_has_data) {
    if (has_data) {
      ++live_stage_count;
    }
  }
  return live_stage_count;
}

bool _HasPipelineStageBufferSlot(
  const algorithm::AlgorithmContainerSet& container_set,
  const std::string& stage_buffer_slot_name);

bool _HasPipelineStageBufferSlot(
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

bool _PrepareCpuPipelineStage0Submission(
  const std::string& stage0_algorithm_name,
  const std::vector<algorithm_management::AlgorithmResourceBinding>& resource_bindings,
  const std::vector<algorithm_management::AlgorithmDescriptorValue>& descriptor_values,
  const std::string& stage_buffer_slot_name,
  CpuPendingPipelineStage0Submission* out_submission,
  std::string* out_error_message) {
  if (!out_submission) {
    if (out_error_message) {
      *out_error_message = "CPU pipeline stage0 submission output pointer is null.";
    }
    return false;
  }

  algorithm_management::AlgorithmObject prepared_object{};
  if (!algorithm_management::PrepareAlgorithmObjectByName(
        stage0_algorithm_name,
        resource_bindings,
        descriptor_values,
        &prepared_object,
        out_error_message)) {
    return false;
  }
  if (!_HasPipelineStageBufferSlot(*prepared_object.mutable_container_set(), stage_buffer_slot_name)) {
    if (out_error_message) {
      *out_error_message =
        "Prepared stage0 submission is missing the required pipeline stage buffer slot '" +
        stage_buffer_slot_name + "'.";
    }
    return false;
  }

  out_submission->prepared_container_set = prepared_object.shared_container_set;
  out_submission->resource_bindings = resource_bindings;
  out_submission->descriptor_values = descriptor_values;
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

struct CpuWorkerQueues {
  std::deque<CpuJobTask> high;
  std::deque<CpuJobTask> normal;
  std::deque<CpuJobTask> low;
};

thread_local int g_current_cpu_worker_index = -1;

class CpuJobSystem {
 public:
  static CpuJobSystem& Instance() {
    static CpuJobSystem instance{};
    return instance;
  }

  bool Init(size_t worker_count) {
    if (worker_count == 0u) {
      _AbortJobExecution("CPU job system requires at least one worker thread.");
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
      if (worker_count_ != worker_count) {
        _AbortJobExecution("CPU job system was already initialized with a different worker count.");
      }
      return true;
    }

    try {
      worker_count_ = worker_count;
      workers_.resize(worker_count_);
      queues_.resize(worker_count_);
      stop_requested_ = false;
      for (size_t i = 0; i < worker_count_; ++i) {
        workers_[i] = std::thread([this, i]() {
          _WorkerLoop(i);
        });
      }
      initialized_ = true;
      return true;
    } catch (...) {
      stop_requested_ = true;
      cv_.notify_all();
      for (std::thread& worker : workers_) {
        if (worker.joinable()) {
          worker.join();
        }
      }
      workers_.clear();
      queues_.clear();
      worker_count_ = 0u;
      stop_requested_ = false;
      initialized_ = false;
      throw;
    }
  }

  void Shutdown() {
    std::vector<std::thread> workers_to_join;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!initialized_) {
        return;
      }
      stop_requested_ = true;
      cv_.notify_all();
      workers_to_join = std::move(workers_);
      queues_.clear();
      worker_count_ = 0u;
      initialized_ = false;
    }

    for (std::thread& worker : workers_to_join) {
      if (worker.joinable()) {
        worker.join();
      }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    stop_requested_ = false;
  }

  bool Initialized() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return initialized_;
  }

  bool SubmitBlocking(
    algorithm_management::AlgorithmJobPriority priority,
    std::function<void(CpuJobCompletion&)> body,
    std::string* out_error_message) {
    auto completion = std::make_shared<CpuJobCompletion>();
    auto done = std::make_shared<std::promise<void>>();
    std::future<void> future = done->get_future();

    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!initialized_) {
        if (out_error_message) {
          *out_error_message = "CPU job system is not initialized.";
        }
        _AbortJobExecution("CPU job system is not initialized.");
      }

      const size_t target_worker_index = _SelectWorkerUnlocked(priority);
      CpuJobTask task{};
      task.priority = priority;
      task.completion = completion;
      task.done = done;
      task.body = std::move(body);
      _QueueForPriority(queues_[target_worker_index], priority).push_back(std::move(task));
    }

    cv_.notify_all();

    if (g_current_cpu_worker_index >= 0) {
      while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        if (!_TryExecuteOneTask(static_cast<size_t>(g_current_cpu_worker_index))) {
          std::this_thread::yield();
        }
      }
    } else {
      future.wait();
    }

    future.get();
    if (!completion->ok) {
      if (out_error_message) {
        *out_error_message = completion->error_message.empty()
          ? "CPU job execution failed."
          : std::move(completion->error_message);
      } else {
        _AbortJobExecution(
          completion->error_message.empty()
            ? "CPU job execution failed."
            : std::move(completion->error_message));
      }
      return false;
    }

    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

 private:
  CpuJobSystem() = default;
  ~CpuJobSystem() {
    Shutdown();
  }

  static std::deque<CpuJobTask>& _QueueForPriority(
    CpuWorkerQueues& queues,
    algorithm_management::AlgorithmJobPriority priority) {
    switch (priority) {
      case algorithm_management::AlgorithmJobPriority::High:
        return queues.high;
      case algorithm_management::AlgorithmJobPriority::Normal:
        return queues.normal;
      case algorithm_management::AlgorithmJobPriority::Low:
        return queues.low;
    }
    return queues.high;
  }

  static const std::deque<CpuJobTask>& _QueueForPriority(
    const CpuWorkerQueues& queues,
    algorithm_management::AlgorithmJobPriority priority) {
    switch (priority) {
      case algorithm_management::AlgorithmJobPriority::High:
        return queues.high;
      case algorithm_management::AlgorithmJobPriority::Normal:
        return queues.normal;
      case algorithm_management::AlgorithmJobPriority::Low:
        return queues.low;
    }
    return queues.high;
  }

  static bool _StealFromQueue(
    std::deque<CpuJobTask>& queue,
    CpuJobTask* out_task) {
    if (!out_task || queue.size() < 2u) {
      return false;
    }

    auto it = queue.begin();
    ++it;
    *out_task = std::move(*it);
    queue.erase(it);
    return true;
  }

  bool _HasWorkUnlocked() const {
    for (const CpuWorkerQueues& queues : queues_) {
      if (!queues.high.empty() || !queues.normal.empty() || !queues.low.empty()) {
        return true;
      }
    }
    return false;
  }

  size_t _QueueWeight(const CpuWorkerQueues& queues) const {
    return queues.high.size() * 9u + queues.normal.size() * 3u + queues.low.size();
  }

  size_t _SelectWorkerUnlocked(algorithm_management::AlgorithmJobPriority priority) const {
    if (workers_.empty()) {
      return 0u;
    }

    size_t best_worker_index = 0u;
    size_t best_weight = std::numeric_limits<size_t>::max();
    for (size_t i = 0; i < queues_.size(); ++i) {
      const size_t weight = _QueueWeight(queues_[i]);
      if (weight < best_weight) {
        best_weight = weight;
        best_worker_index = i;
      }
    }

    (void)priority;
    return best_worker_index;
  }

  bool _TryPopLocalTaskLocked(
    size_t worker_index,
    CpuJobTask* out_task) {
    if (!out_task || worker_index >= queues_.size()) {
      return false;
    }

    CpuWorkerQueues& queues = queues_[worker_index];
    if (!queues.high.empty()) {
      *out_task = std::move(queues.high.front());
      queues.high.pop_front();
      return true;
    }
    if (!queues.normal.empty()) {
      *out_task = std::move(queues.normal.front());
      queues.normal.pop_front();
      return true;
    }
    if (!queues.low.empty()) {
      *out_task = std::move(queues.low.front());
      queues.low.pop_front();
      return true;
    }
    return false;
  }

  bool _TryStealTaskLocked(
    size_t thief_index,
    CpuJobTask* out_task) {
    if (!out_task || queues_.empty()) {
      return false;
    }

    for (size_t offset = 1u; offset < queues_.size(); ++offset) {
      const size_t victim_index = (thief_index + offset) % queues_.size();
      CpuWorkerQueues& victim = queues_[victim_index];
      if (_StealFromQueue(victim.high, out_task)) {
        return true;
      }
      if (_StealFromQueue(victim.normal, out_task)) {
        return true;
      }
      if (_StealFromQueue(victim.low, out_task)) {
        return true;
      }
    }

    return false;
  }

  bool _TryAcquireTaskLocked(
    int worker_index,
    CpuJobTask* out_task) {
    if (worker_index >= 0) {
      if (_TryPopLocalTaskLocked(static_cast<size_t>(worker_index), out_task)) {
        return true;
      }
      return _TryStealTaskLocked(static_cast<size_t>(worker_index), out_task);
    }

    for (size_t i = 0; i < queues_.size(); ++i) {
      if (_TryPopLocalTaskLocked(i, out_task)) {
        return true;
      }
    }
    return _TryStealTaskLocked(0u, out_task);
  }

  bool _TryExecuteOneTask(size_t worker_index) {
    CpuJobTask task{};
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!initialized_) {
        return false;
      }
      if (!_TryAcquireTaskLocked(static_cast<int>(worker_index), &task)) {
        return false;
      }
    }

    try {
      if (task.body && task.completion) {
        task.body(*task.completion);
      } else if (task.completion) {
        task.completion->ok = false;
        task.completion->error_message = "CPU job task body is missing.";
      }
    } catch (const std::exception& ex) {
      if (task.completion) {
        task.completion->ok = false;
        task.completion->error_message = ex.what();
      }
    } catch (...) {
      if (task.completion) {
        task.completion->ok = false;
        task.completion->error_message = "CPU job task failed with an unknown error.";
      }
    }

    if (task.done) {
      task.done->set_value();
    }
    return true;
  }

  void _WorkerLoop(size_t worker_index) {
    g_current_cpu_worker_index = static_cast<int>(worker_index);
    while (true) {
      CpuJobTask task{};
      {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [&]() {
          return stop_requested_ || _HasWorkUnlocked();
        });
        if (stop_requested_ && !_HasWorkUnlocked()) {
          break;
        }
        if (!_TryAcquireTaskLocked(static_cast<int>(worker_index), &task)) {
          continue;
        }
      }

      try {
        if (task.body && task.completion) {
          task.body(*task.completion);
        } else if (task.completion) {
          task.completion->ok = false;
          task.completion->error_message = "CPU job task body is missing.";
        }
      } catch (const std::exception& ex) {
        if (task.completion) {
          task.completion->ok = false;
          task.completion->error_message = ex.what();
        }
      } catch (...) {
        if (task.completion) {
          task.completion->ok = false;
          task.completion->error_message = "CPU job task failed with an unknown error.";
        }
      }

      if (task.done) {
        task.done->set_value();
      }
    }
    g_current_cpu_worker_index = -1;
  }

  mutable std::mutex mutex_{};
  std::condition_variable cv_{};
  bool stop_requested_{false};
  bool initialized_{false};
  size_t worker_count_{0u};
  std::vector<CpuWorkerQueues> queues_{};
  std::vector<std::thread> workers_{};
};

class CpuJobRuntimeSystem {
 public:
  static CpuJobRuntimeSystem& Instance() {
    static CpuJobRuntimeSystem instance{};
    return instance;
  }

  void Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    pipeline_registrations_.clear();
    pipeline_runtime_states_.clear();
  }

  bool ExecuteCpuTick(
    const algorithm_management::AlgorithmObject& object,
    const algorithm_management::AgentTickContext& context,
    const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
    algorithm::AlgorithmContainerSet* container_set,
    common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
    algorithm_management::AlgorithmPackageDebugState* out_debug_state,
    std::string* out_error_message) {
    if (!container_set) {
      if (out_error_message) {
        *out_error_message = "AlgorithmContainerSet pointer is null.";
      }
      _AbortJobExecution("AlgorithmContainerSet pointer is null.");
    }
    if (!object.temporaryTest_main_thread_executor) {
      if (out_error_message) {
        *out_error_message = "CPU job executor is unavailable.";
      }
      _AbortJobExecution("CPU job executor is unavailable.");
    }

    auto body = [
      &object,
      &context,
      &agent_to_algorithm_signal,
      container_set,
      out_algorithm_to_agent_signal,
      out_debug_state](CpuJobCompletion& completion) {
      completion.ok = object.temporaryTest_main_thread_executor->temporaryTestExecuteOnMainThread(
        context,
        object.algorithm_profile,
        agent_to_algorithm_signal,
        container_set,
        out_algorithm_to_agent_signal,
        out_debug_state);
      if (!completion.ok && completion.error_message.empty()) {
        completion.error_message = "CPU algorithm execution failed.";
      }
    };

    return CpuJobSystem::Instance().SubmitBlocking(
      context.job_priority,
      std::move(body),
      out_error_message);
  }

  bool RegisterPipeline(
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
          found->second.max_concurrent_stage0_submissions != registration.max_concurrent_stage0_submissions ||
          found->second.mandatory_stage_buffer_slot_name != registration.mandatory_stage_buffer_slot_name) {
        if (out_error_message) {
          *out_error_message =
            "CPU pipeline registration conflicts with an existing mounted pipeline: " + registration.pipeline_name;
        }
        return false;
      }
      if (out_error_message) {
        out_error_message->clear();
      }
      return true;
    }

    pipeline_registrations_.emplace(registration.pipeline_name, registration);
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  bool RegisterPipelineRuntime(
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
    if (registration_it == pipeline_registrations_.end()) {
      if (out_error_message) {
        *out_error_message = "CPU pipeline runtime registration requires an existing pipeline registration.";
      }
      return false;
    }

    CpuPipelineRuntimeState owner_runtime_state = runtime_state;
    owner_runtime_state.owner_agent_name = owner_agent_name;
    pipeline_runtime_states_[pipeline_name][owner_agent_name] = std::move(owner_runtime_state);
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  bool EnqueuePipelineStage0Submission(
    const std::string& pipeline_name,
    const std::string& owner_agent_name,
    const std::string& stage0_algorithm_name,
    const std::vector<algorithm_management::AlgorithmResourceBinding>& resource_bindings,
    const std::vector<algorithm_management::AlgorithmDescriptorValue>& descriptor_values,
    std::string* out_error_message) {
    if (pipeline_name.empty()) {
      if (out_error_message) {
        *out_error_message = "CPU pipeline stage0 submission requires a pipeline name.";
      }
      return false;
    }
    if (stage0_algorithm_name.empty()) {
      if (out_error_message) {
        *out_error_message = "CPU pipeline stage0 submission requires a stage0 algorithm name.";
      }
      return false;
    }

    CpuPipelineRegistration registration{};
    CpuPipelineRuntimeState pipeline_runtime_state{};
    {
      std::lock_guard<std::mutex> lock(mutex_);
      const auto registration_it = pipeline_registrations_.find(pipeline_name);
      if (registration_it == pipeline_registrations_.end()) {
        if (out_error_message) {
          *out_error_message = "Mounted CPU pipeline registration is unavailable.";
        }
        return false;
      }
      const auto runtime_it = pipeline_runtime_states_.find(pipeline_name);
      if (runtime_it == pipeline_runtime_states_.end()) {
        if (out_error_message) {
          *out_error_message = "Mounted CPU pipeline runtime state is unavailable.";
        }
        return false;
      }
      registration = registration_it->second;
      const auto owner_found = runtime_it->second.find(owner_agent_name);
      if (owner_found == runtime_it->second.end()) {
        if (out_error_message) {
          *out_error_message = "Mounted CPU pipeline runtime state is unavailable for the selected agent.";
        }
        return false;
      }
      pipeline_runtime_state = owner_found->second;
    }

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
    pipeline_runtime_state.owner_agent_name = owner_agent_name;

    CpuPendingPipelineStage0Submission submission{};
    submission.owner_agent_name = owner_agent_name;
    std::string prepare_error_message;
    if (!_PrepareCpuPipelineStage0Submission(
          stage0_algorithm_name,
          resource_bindings,
          descriptor_values,
          registration.mandatory_stage_buffer_slot_name,
          &submission,
          &prepare_error_message)) {
      if (out_error_message) {
        *out_error_message = prepare_error_message.empty()
          ? "Failed to prepare the CPU pipeline stage0 submission."
          : std::move(prepare_error_message);
      }
      return false;
    }

    submission.lane_id = pipeline_runtime_state.next_lane_id;
    submission.loop_lane_active = false;
    CpuPipelineLaneRuntimeState queued_lane_state{};
    queued_lane_state.owner_agent_name = owner_agent_name;
    queued_lane_state.lane_id = submission.lane_id;
    queued_lane_state.loop_lane_active = false;
    queued_lane_state.standard_container_set = submission.prepared_container_set;
    queued_lane_state.resource_bindings = resource_bindings;
    queued_lane_state.descriptor_values = descriptor_values;
    queued_lane_state.stage_has_data.assign(registration.stage_count, false);
    queued_lane_state.inter_stage_buffer.standard_container_slot_name =
      registration.mandatory_stage_buffer_slot_name;
    queued_lane_state.inter_stage_buffer.valid = true;
    queued_lane_state.valid = true;
    pipeline_runtime_state.lanes.push_back(std::move(queued_lane_state));
    ++pipeline_runtime_state.next_lane_id;

    pipeline_runtime_state.pending_stage0_submissions.push_back(std::move(submission));
    {
      std::lock_guard<std::mutex> lock(mutex_);
      pipeline_runtime_states_[pipeline_name][owner_agent_name] = std::move(pipeline_runtime_state);
    }
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  bool TickMountedPipeline(
    std::vector<algorithm_management::AlgorithmObject>* mounted_objects,
    size_t begin_index,
    size_t end_index,
    const std::string& owner_agent_name,
    const algorithm_management::AgentTickContext& context,
    const std::vector<bool>& allow_tick_mask,
    const std::vector<algorithm_management::AlgorithmAssemblyState>& assembly_states,
    std::vector<algorithm_management::AgentAlgorithmRuntimeState>* inout_runtime_states,
    common_data::AlgorithmToAgentSignal* out_pipeline_signal,
    bool* out_pipeline_processing_failed,
    std::string* out_error_message) {
    auto set_error = [&](std::string message) {
      if (out_error_message) {
        *out_error_message = std::move(message);
      }
    };

    if (!mounted_objects || !inout_runtime_states || !out_pipeline_signal || !out_pipeline_processing_failed) {
      set_error("CPU pipeline tick received a null output pointer.");
      return false;
    }
    if (begin_index >= end_index || end_index > mounted_objects->size()) {
      set_error("CPU pipeline tick received an invalid pipeline range.");
      return false;
    }

    std::vector<algorithm_management::AlgorithmObject>& algorithm_objects = *mounted_objects;
    std::vector<algorithm_management::AgentAlgorithmRuntimeState>& updated_runtime_states = *inout_runtime_states;
    updated_runtime_states.resize(algorithm_objects.size());
    *out_pipeline_signal = {};
    *out_pipeline_processing_failed = false;

    algorithm_management::AlgorithmObject& root_object = algorithm_objects[begin_index];
    if (root_object.execution_preference != algorithm_management::AlgorithmExecutionPreference::Cpu) {
      set_error("CPU pipeline tick received a non-CPU pipeline root.");
      return false;
    }
    const bool pipeline_stage_debug_all = root_object.pipeline_stage_debug_all;
    const uint32_t pipeline_stage_debug_index = root_object.pipeline_stage_debug_index;

    CpuPipelineRuntimeState pipeline_state{};
    {
      std::lock_guard<std::mutex> lock(mutex_);
      const auto runtime_it = pipeline_runtime_states_.find(root_object.pipeline_name);
      if (runtime_it == pipeline_runtime_states_.end()) {
        set_error("CPU pipeline runtime state is unavailable.");
        return false;
      }
      const auto owner_it = runtime_it->second.find(owner_agent_name);
      if (owner_it == runtime_it->second.end()) {
        set_error("CPU pipeline runtime state is unavailable for the selected agent.");
        return false;
      }
      pipeline_state = owner_it->second;
    }
    const auto flush_pipeline_state = [&]() {
      std::lock_guard<std::mutex> lock(mutex_);
      pipeline_runtime_states_[root_object.pipeline_name][owner_agent_name] = pipeline_state;
    };
    const struct PipelineRuntimeFlushScope {
      const decltype(flush_pipeline_state)& flush;
      ~PipelineRuntimeFlushScope() { flush(); }
    } pipeline_runtime_flush_scope{flush_pipeline_state};

    const size_t pipeline_stage_count = end_index - begin_index;
    if (pipeline_state.lanes.empty()) {
      CpuPipelineLaneRuntimeState fallback_lane_state{};
      std::string fallback_lane_error_message;
      if (!_TryBuildInitialPipelineLaneRuntimeState(
            algorithm_objects[begin_index],
            pipeline_stage_count,
            owner_agent_name,
            pipeline_state.topology ==
              algorithm_management::AlgorithmPipelineTopology::Circular,
            pipeline_state.next_lane_id,
            pipeline_state.mandatory_stage_buffer_slot_name,
            &fallback_lane_state,
            &fallback_lane_error_message)) {
        const std::string failure_message = fallback_lane_error_message.empty()
          ? std::string("Failed to rebuild the primary pipeline lane runtime state.")
          : std::move(fallback_lane_error_message);
        DEBUG_TOOL_ASSERT(false, failure_message.c_str());
        set_error(failure_message);
        *out_pipeline_signal = {};
        out_pipeline_signal->stop_requested = true;
        *out_pipeline_processing_failed = true;
        return false;
      }
      pipeline_state.current_lane_id = fallback_lane_state.lane_id;
      pipeline_state.lanes.push_back(std::move(fallback_lane_state));
      ++pipeline_state.next_lane_id;
    }

    _SyncPipelineLegacyStageStateFromPrimaryLane(&pipeline_state, pipeline_stage_count);
    if (pipeline_state.stage_has_data.size() != pipeline_stage_count) {
      pipeline_state.stage_has_data.assign(pipeline_stage_count, false);
    }
    CpuPipelineLaneRuntimeState* primary_lane_state =
      _FindPrimaryPipelineLaneRuntimeState(&pipeline_state);
    DEBUG_TOOL_ASSERT(primary_lane_state != nullptr, "Primary pipeline lane runtime state is unavailable.");
    if (!primary_lane_state) {
      set_error("Primary pipeline lane runtime state is unavailable.");
      out_pipeline_signal->stop_requested = true;
      *out_pipeline_processing_failed = true;
      return false;
    }

    PipelineGroupProgressState previous_progress_state{};
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
      if (pipeline_state.sync_mode ==
          algorithm_management::AlgorithmPipelineSyncMode::Forced) {
        previous_progress_state.stage_runtime_stats =
          updated_runtime_states[begin_index].pipeline_stage_runtime_stats;
      }
    }
    PipelineGroupProgressState progress_state = previous_progress_state;
    progress_state.stall_reason.clear();
    const bool collect_pipeline_timing =
      pipeline_state.sync_mode == algorithm_management::AlgorithmPipelineSyncMode::Forced;
    std::vector<algorithm_management::AlgorithmPipelineStageRuntimeStat>* pipeline_stage_runtime_stats =
      collect_pipeline_timing ? &progress_state.stage_runtime_stats : nullptr;
    if (collect_pipeline_timing) {
      progress_state.stage_runtime_stats.clear();
      progress_state.stage_runtime_stats.reserve(end_index - begin_index);
    } else {
      progress_state.stage_runtime_stats.clear();
    }

    algorithm_objects[begin_index].SetContainerSet(primary_lane_state->standard_container_set);
    algorithm_objects[begin_index].resource_bindings = primary_lane_state->resource_bindings;
    algorithm_objects[begin_index].descriptor_values = primary_lane_state->descriptor_values;

    std::vector<bool> current_stage_has_data = primary_lane_state->stage_has_data;
    std::vector<bool> next_stage_has_data(end_index - begin_index, false);

    const bool stage0_allow_tick = begin_index < allow_tick_mask.size() ? allow_tick_mask[begin_index] : true;
    const bool stage0_is_ready =
      begin_index < assembly_states.size() &&
      assembly_states[begin_index] == algorithm_management::AlgorithmAssemblyState::Ready;
      if (!current_stage_has_data.empty() &&
        !current_stage_has_data.front() &&
        stage0_allow_tick &&
        stage0_is_ready &&
        !pipeline_state.pending_stage0_submissions.empty() &&
        (pipeline_stage_debug_all || pipeline_stage_debug_index == 0u)) {
      algorithm_management::AlgorithmObject& stage0_object = algorithm_objects[begin_index];
      CpuPendingPipelineStage0Submission submission =
        std::move(pipeline_state.pending_stage0_submissions.front());
      pipeline_state.pending_stage0_submissions.erase(pipeline_state.pending_stage0_submissions.begin());
      if (_CountLiveCpuPipelineStages(current_stage_has_data) == 0u &&
          pipeline_state.current_lane_id != 0u &&
          pipeline_state.current_lane_id != submission.lane_id) {
        if (CpuPipelineLaneRuntimeState* previous_lane_state =
              _FindPipelineLaneRuntimeStateById(&pipeline_state, pipeline_state.current_lane_id)) {
          previous_lane_state->valid = false;
        }
      }
      submission.owner_agent_name = owner_agent_name;
      pipeline_state.current_lane_id = submission.lane_id;
      primary_lane_state = _FindPrimaryPipelineLaneRuntimeState(&pipeline_state);
      DEBUG_TOOL_ASSERT(primary_lane_state != nullptr, "Submitted pipeline lane runtime state is unavailable.");
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
    if (!_BuildPipelineStageContainerSets(
          algorithm_objects,
          begin_index,
          end_index,
          &stage_container_sets,
          &stage_set_error_message)) {
      const std::string failure_message = stage_set_error_message.empty()
        ? std::string("Failed to build pipeline stage container sets.")
        : std::move(stage_set_error_message);
      DEBUG_TOOL_ASSERT(false, failure_message.c_str());
      set_error(failure_message);
      out_pipeline_signal->stop_requested = true;
      *out_pipeline_processing_failed = true;
      return false;
    }

    std::vector<size_t> executable_indices{};
    executable_indices.reserve(end_index - begin_index);
    std::vector<bool> stage_allow_tick(end_index - begin_index, false);
    std::vector<bool> stage_is_ready(end_index - begin_index, false);
    std::vector<bool> stage_launch_once_completed(end_index - begin_index, false);
    std::vector<bool> stage_execute_mask(end_index - begin_index, false);

    for (size_t index = begin_index; index < end_index; ++index) {
      const size_t stage_offset = index - begin_index;
      algorithm_management::AlgorithmObject& object = algorithm_objects[index];
      algorithm_management::AgentAlgorithmRuntimeState runtime_state{};
      if (index < updated_runtime_states.size()) {
        runtime_state = updated_runtime_states[index];
      } else {
        runtime_state.algorithm_name = object.algorithm_profile.algorithm_name;
      }
      _ResetRuntimeStateBase(runtime_state, &runtime_state);
      updated_runtime_states[index] = std::move(runtime_state);

      if (collect_pipeline_timing) {
        progress_state.stage_runtime_stats.push_back(algorithm_management::AlgorithmPipelineStageRuntimeStat{
          .stage_name = object.algorithm_profile.algorithm_name,
          .elapsed_seconds = 0.0f,
          .reason = {},
        });
      }

      stage_allow_tick[stage_offset] = index < allow_tick_mask.size() ? allow_tick_mask[index] : true;
      stage_is_ready[stage_offset] =
        index < assembly_states.size() &&
        assembly_states[index] == algorithm_management::AlgorithmAssemblyState::Ready;
      const bool launch_once_then_hold =
        object.tick_lifetime == algorithm_management::AlgorithmTickLifetime::LaunchOnceThenHold;
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

    for (size_t pass = 0u; pass < pipeline_stage_count; ++pass) {
      bool changed = false;
      for (size_t stage_offset = 0u; stage_offset < pipeline_stage_count; ++stage_offset) {
        if (!stage_execute_mask[stage_offset]) {
          continue;
        }
      }
      if (!changed) {
        break;
      }
    }

    for (size_t index = begin_index; index < end_index; ++index) {
      const size_t stage_offset = index - begin_index;
      algorithm_management::AlgorithmObject& object = algorithm_objects[index];
      algorithm_management::AgentAlgorithmRuntimeState& runtime_state = updated_runtime_states[index];
      const bool has_data = stage_offset < current_stage_has_data.size() && current_stage_has_data[stage_offset];
      const bool execute_now = stage_offset < stage_execute_mask.size() && stage_execute_mask[stage_offset];
      if (!execute_now) {
        _SetPipelineStageRuntimeReason(
          pipeline_stage_runtime_stats,
          object.algorithm_profile.algorithm_name,
          !has_data
            ? _BuildPipelineStageNoInputReason(
                stage_offset == 0u,
                !pipeline_state.pending_stage0_submissions.empty())
            : (stage_allow_tick[stage_offset] &&
               stage_is_ready[stage_offset] &&
               !stage_launch_once_completed[stage_offset])
                ? "Stage is holding data, but the downstream stage cannot accept output this tick."
                : _BuildPipelineStageIdleReason(
                    stage_allow_tick[stage_offset],
                    stage_is_ready[stage_offset],
                    stage_launch_once_completed[stage_offset]));
        if (!_TickAlgorithmObject(
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
        _MergeAlgorithmToAgentSignal(runtime_state.algorithm_to_agent_signal, out_pipeline_signal);
        continue;
      }
      _SetPipelineStageRuntimeReason(
        pipeline_stage_runtime_stats,
        object.algorithm_profile.algorithm_name,
        "Stage executed.");
      executable_indices.push_back(index);
    }

    if (!_PipelineGroupIsExecutable(executable_indices)) {
      _CommitPipelineStageStateToPrimaryLane(&pipeline_state, next_stage_has_data);
      const bool pipeline_has_live_token = _CountLiveCpuPipelineStages(pipeline_state.stage_has_data) > 0u;
      const uint64_t current_signature =
        _HashPipelineGroupState(
          algorithm_objects,
          executable_indices,
          pipeline_state.current_lane_id,
          _CountValidPipelineLanes(pipeline_state),
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
        _UpdatePipelineGroupProgressState(
          previous_progress_state,
          current_signature,
          true,
          context.dt_seconds,
          &progress_state);
        progress_state.stall_reason = "No pipeline stage was executable during this tick.";
      }
      if (begin_index < updated_runtime_states.size()) {
        updated_runtime_states[begin_index].pipeline_progress_signature = progress_state.signature;
        updated_runtime_states[begin_index].pipeline_progress_signature_valid = progress_state.signature_valid;
        updated_runtime_states[begin_index].pipeline_no_progress_seconds = progress_state.no_progress_seconds;
        updated_runtime_states[begin_index].pipeline_stall_report_requested =
          progress_state.stall_report_requested;
        updated_runtime_states[begin_index].pipeline_stall_reported = progress_state.stall_reported;
        updated_runtime_states[begin_index].pipeline_stall_reason = std::move(progress_state.stall_reason);
        if (collect_pipeline_timing) {
          updated_runtime_states[begin_index].pipeline_total_elapsed_seconds =
            progress_state.total_elapsed_seconds;
          updated_runtime_states[begin_index].pipeline_stage_runtime_stats =
            std::move(progress_state.stage_runtime_stats);
        } else {
          updated_runtime_states[begin_index].pipeline_total_elapsed_seconds = 0.0f;
          updated_runtime_states[begin_index].pipeline_stage_runtime_stats.clear();
        }
      }
      if (out_error_message) {
        out_error_message->clear();
      }
      return true;
    }

    for (size_t index : executable_indices) {
      algorithm_management::AlgorithmObject& object = algorithm_objects[index];
      algorithm_management::AgentAlgorithmRuntimeState& runtime_state = updated_runtime_states[index];
      const auto stage_begin = std::chrono::steady_clock::now();
      algorithm_support::PipelineStageBridge bridge(object.runtime_transfer_map);
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
        DEBUG_TOOL_ASSERT(false, failure_message.c_str());
        runtime_state.algorithm_to_agent_signal.stop_requested = true;
        _SetPipelineStageRuntimeReason(
          pipeline_stage_runtime_stats,
          object.algorithm_profile.algorithm_name,
          "Ingress debug capture failed: " + failure_message);
        _MergeAlgorithmToAgentSignal(runtime_state.algorithm_to_agent_signal, out_pipeline_signal);
        set_error(failure_message);
        *out_pipeline_processing_failed = true;
        return false;
      }
      const auto stage_end = std::chrono::steady_clock::now();
      _AddPipelineStageRuntimeElapsed(
        pipeline_stage_runtime_stats,
        object.algorithm_profile.algorithm_name,
        std::chrono::duration<float>(stage_end - stage_begin).count());
    }

    for (size_t index : executable_indices) {
      algorithm_management::AlgorithmObject& object = algorithm_objects[index];
      algorithm_management::AgentAlgorithmRuntimeState& runtime_state = updated_runtime_states[index];
      const bool allow_tick = index < allow_tick_mask.size() ? allow_tick_mask[index] : true;
      const bool is_ready =
        index < assembly_states.size() &&
        assembly_states[index] == algorithm_management::AlgorithmAssemblyState::Ready;
      const auto stage_begin = std::chrono::steady_clock::now();
      if (!_TickAlgorithmObject(object, context, allow_tick, is_ready, true, &runtime_state)) {
        runtime_state.algorithm_to_agent_signal.stop_requested = true;
        _SetPipelineStageRuntimeReason(
          pipeline_stage_runtime_stats,
          object.algorithm_profile.algorithm_name,
          "Execution failed.");
        _MergeAlgorithmToAgentSignal(runtime_state.algorithm_to_agent_signal, out_pipeline_signal);
        set_error("CPU pipeline stage execution failed.");
        *out_pipeline_processing_failed = true;
        return false;
      }
      const auto stage_end = std::chrono::steady_clock::now();
      _AddPipelineStageRuntimeElapsed(
        pipeline_stage_runtime_stats,
        object.algorithm_profile.algorithm_name,
        std::chrono::duration<float>(stage_end - stage_begin).count());
      _MergeAlgorithmToAgentSignal(runtime_state.algorithm_to_agent_signal, out_pipeline_signal);
    }

    for (size_t index : executable_indices) {
      algorithm_management::AlgorithmObject& object = algorithm_objects[index];
      const algorithm::AlgorithmContainerSet* source_container_set = object.container_set();
      if (!source_container_set) {
        DEBUG_TOOL_ASSERT(false, "Pipeline stage source container set is unavailable.");
        updated_runtime_states[index].algorithm_to_agent_signal.stop_requested = true;
        _SetPipelineStageRuntimeReason(
          pipeline_stage_runtime_stats,
          object.algorithm_profile.algorithm_name,
          "Egress failed: source container set is unavailable.");
        _MergeAlgorithmToAgentSignal(updated_runtime_states[index].algorithm_to_agent_signal, out_pipeline_signal);
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
        if (_CollectPipelineExitReflectionSnapshot(
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
        if (pipeline_state.topology ==
            algorithm_management::AlgorithmPipelineTopology::Circular) {
          std::string loopback_error_message;
          if (!_CopyPipelineCircularLoopback(
                *source_container_set,
                &algorithm_objects[begin_index],
                &loopback_error_message)) {
            const std::string failure_message = loopback_error_message.empty()
              ? std::string("Failed to emit circular pipeline loopback output.")
              : std::move(loopback_error_message);
            DEBUG_TOOL_ASSERT(false, failure_message.c_str());
            updated_runtime_states[index].algorithm_to_agent_signal.stop_requested = true;
            _SetPipelineStageRuntimeReason(
              pipeline_stage_runtime_stats,
              object.algorithm_profile.algorithm_name,
              "Egress failed: " + failure_message);
            _MergeAlgorithmToAgentSignal(
              updated_runtime_states[index].algorithm_to_agent_signal,
              out_pipeline_signal);
            set_error(failure_message);
            *out_pipeline_processing_failed = true;
            return false;
          }
          next_stage_has_data.front() = true;
        }
      } else {
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
            pipeline_stage_runtime_stats,
            object.algorithm_profile.algorithm_name,
            "Egress failed: " + failure_message);
          _MergeAlgorithmToAgentSignal(
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
        DEBUG_TOOL_ASSERT(false, failure_message.c_str());
        updated_runtime_states[index].algorithm_to_agent_signal.stop_requested = true;
        _SetPipelineStageRuntimeReason(
          pipeline_stage_runtime_stats,
          object.algorithm_profile.algorithm_name,
          "Egress debug capture failed: " + failure_message);
        _MergeAlgorithmToAgentSignal(
          updated_runtime_states[index].algorithm_to_agent_signal,
          out_pipeline_signal);
        set_error(failure_message);
        *out_pipeline_processing_failed = true;
        return false;
      }
      const auto stage_end = std::chrono::steady_clock::now();
      _AddPipelineStageRuntimeElapsed(
        pipeline_stage_runtime_stats,
        object.algorithm_profile.algorithm_name,
        std::chrono::duration<float>(stage_end - stage_begin).count());
    }

    _CommitPipelineStageStateToPrimaryLane(&pipeline_state, next_stage_has_data);
    const uint64_t current_signature =
      _HashPipelineGroupState(
        algorithm_objects,
        executable_indices,
        pipeline_state.current_lane_id,
        _CountValidPipelineLanes(pipeline_state),
        pipeline_state.stage_has_data,
        pipeline_state.pending_stage0_submissions.size(),
          pipeline_state.stage0_saturated,
        updated_runtime_states,
        begin_index,
        end_index);
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
        "Pipeline signature did not change after execute and bridge output completed.";
      for (size_t index : executable_indices) {
        algorithm_management::AlgorithmObject& object = algorithm_objects[index];
        _SetPipelineStageRuntimeReason(
          pipeline_stage_runtime_stats,
          object.algorithm_profile.algorithm_name,
          "Stage executed, but the observable pipeline state did not change.");
      }
    }
    if (begin_index < updated_runtime_states.size()) {
      updated_runtime_states[begin_index].pipeline_progress_signature = progress_state.signature;
      updated_runtime_states[begin_index].pipeline_progress_signature_valid = progress_state.signature_valid;
      updated_runtime_states[begin_index].pipeline_no_progress_seconds = progress_state.no_progress_seconds;
      updated_runtime_states[begin_index].pipeline_stall_report_requested =
        progress_state.stall_report_requested;
      updated_runtime_states[begin_index].pipeline_stall_reported = progress_state.stall_reported;
      updated_runtime_states[begin_index].pipeline_stall_reason = std::move(progress_state.stall_reason);
      if (collect_pipeline_timing) {
        updated_runtime_states[begin_index].pipeline_total_elapsed_seconds =
          progress_state.total_elapsed_seconds;
        updated_runtime_states[begin_index].pipeline_stage_runtime_stats =
          std::move(progress_state.stage_runtime_stats);
      } else {
        updated_runtime_states[begin_index].pipeline_total_elapsed_seconds = 0.0f;
        updated_runtime_states[begin_index].pipeline_stage_runtime_stats.clear();
      }
    }

    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  void UnregisterPipeline(const std::string& pipeline_name, const std::string& owner_agent_name) {
    if (pipeline_name.empty()) {
      return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    pipeline_registrations_.erase(pipeline_name);
    const auto runtime_states_it = pipeline_runtime_states_.find(pipeline_name);
    if (runtime_states_it != pipeline_runtime_states_.end()) {
      runtime_states_it->second.erase(owner_agent_name);
      if (runtime_states_it->second.empty()) {
        pipeline_runtime_states_.erase(runtime_states_it);
      }
    }
  }

  bool TryGetPipelineRegistration(
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

  bool TryGetPipelineRuntime(
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

  bool UpdatePipelineRuntime(
    const std::string& pipeline_name,
    const std::string& owner_agent_name,
    const CpuPipelineRuntimeState& runtime_state,
    std::string* out_error_message) {
    if (pipeline_name.empty()) {
      if (out_error_message) {
        *out_error_message = "CPU pipeline runtime update requires a pipeline name.";
      }
      return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const auto registration_it = pipeline_registrations_.find(pipeline_name);
    if (registration_it == pipeline_registrations_.end()) {
      if (out_error_message) {
        *out_error_message = "CPU pipeline runtime update requires an existing pipeline registration.";
      }
      return false;
    }

    CpuPipelineRuntimeState owner_runtime_state = runtime_state;
    owner_runtime_state.owner_agent_name = owner_agent_name;
    pipeline_runtime_states_[pipeline_name][owner_agent_name] = std::move(owner_runtime_state);
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

 private:
  mutable std::mutex mutex_{};
  std::unordered_map<std::string, CpuPipelineRegistration> pipeline_registrations_{};
  std::unordered_map<std::string, std::unordered_map<std::string, CpuPipelineRuntimeState>> pipeline_runtime_states_{};
};

}  // namespace

bool InitializeJobSystem(size_t worker_count) {
  return CpuJobSystem::Instance().Init(worker_count);
}

void ShutdownJobSystem() {
  CpuJobSystem::Instance().Shutdown();
}

bool IsJobSystemInitialized() {
  return CpuJobSystem::Instance().Initialized();
}

namespace job_cpu {

void Clear() {
  CpuJobRuntimeSystem::Instance().Clear();
}

bool Execute(
  const algorithm_management::AlgorithmObject& object,
  const algorithm_management::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  algorithm::AlgorithmContainerSet* container_set,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  algorithm_management::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message) {
  return CpuJobRuntimeSystem::Instance().ExecuteCpuTick(
    object,
    context,
    agent_to_algorithm_signal,
    container_set,
    out_algorithm_to_agent_signal,
    out_debug_state,
    out_error_message);
}

bool RegisterPipeline(
  const CpuPipelineRegistration& registration,
  std::string* out_error_message) {
  return CpuJobRuntimeSystem::Instance().RegisterPipeline(registration, out_error_message);
}

bool RegisterPipelineRuntime(
  const std::string& pipeline_name,
  const std::string& owner_agent_name,
  const CpuPipelineRuntimeState& runtime_state,
  std::string* out_error_message) {
  return CpuJobRuntimeSystem::Instance().RegisterPipelineRuntime(
    pipeline_name,
    owner_agent_name,
    runtime_state,
    out_error_message);
}

bool EnqueuePipelineStage0Submission(
  const std::string& pipeline_name,
  const std::string& owner_agent_name,
  const std::string& stage0_algorithm_name,
  const std::vector<algorithm_management::AlgorithmResourceBinding>& resource_bindings,
  const std::vector<algorithm_management::AlgorithmDescriptorValue>& descriptor_values,
  std::string* out_error_message) {
  return CpuJobRuntimeSystem::Instance().EnqueuePipelineStage0Submission(
    pipeline_name,
    owner_agent_name,
    stage0_algorithm_name,
    resource_bindings,
    descriptor_values,
    out_error_message);
}

bool TickMountedPipeline(
  std::vector<algorithm_management::AlgorithmObject>* mounted_objects,
  size_t begin_index,
  size_t end_index,
  const std::string& owner_agent_name,
  const algorithm_management::AgentTickContext& context,
  const std::vector<bool>& allow_tick_mask,
  const std::vector<algorithm_management::AlgorithmAssemblyState>& assembly_states,
  std::vector<algorithm_management::AgentAlgorithmRuntimeState>* inout_runtime_states,
  common_data::AlgorithmToAgentSignal* out_pipeline_signal,
  bool* out_pipeline_processing_failed,
  std::string* out_error_message) {
  return CpuJobRuntimeSystem::Instance().TickMountedPipeline(
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
}

void UnregisterPipeline(const std::string& pipeline_name, const std::string& owner_agent_name) {
  CpuJobRuntimeSystem::Instance().UnregisterPipeline(pipeline_name, owner_agent_name);
}

bool TryGetPipelineRegistration(
  const std::string& pipeline_name,
  CpuPipelineRegistration* out_registration) {
  return CpuJobRuntimeSystem::Instance().TryGetPipelineRegistration(
    pipeline_name,
    out_registration);
}

bool TryGetPipelineRuntime(
  const std::string& pipeline_name,
  const std::string& owner_agent_name,
  CpuPipelineRuntimeState* out_runtime_state) {
  return CpuJobRuntimeSystem::Instance().TryGetPipelineRuntime(
    pipeline_name,
    owner_agent_name,
    out_runtime_state);
}

bool UpdatePipelineRuntime(
  const std::string& pipeline_name,
  const std::string& owner_agent_name,
  const CpuPipelineRuntimeState& runtime_state,
  std::string* out_error_message) {
  return CpuJobRuntimeSystem::Instance().UpdatePipelineRuntime(
    pipeline_name,
    owner_agent_name,
    runtime_state,
    out_error_message);
}

}  // namespace job_cpu

namespace job_gpu {

void Clear() {
  ClearGpuRuntime();
}

bool Execute(
  const algorithm_management::AlgorithmObject& object,
  algorithm::AlgorithmContainerSet* container_set,
  const algorithm_management::AgentTickContext& context,
  std::string* out_error_message) {
  return TryExecuteGpuTick(object, container_set, context, out_error_message);
}

bool Synchronize(
  const algorithm_management::AlgorithmObject& object,
  algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message) {
  return TrySynchronizeGpuTickState(object, container_set, out_error_message);
}

}  // namespace job_gpu

}  // namespace runtime_systems
